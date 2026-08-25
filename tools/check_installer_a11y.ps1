<#
.SYNOPSIS
    Walks the installer's wizard pages and reports what a screen reader would be told.

.DESCRIPTION
    Compile the probe build first - it is the same wizard without elevation or payload:

        ISCC.exe /DStageDir=<stage> /DProbe /O<dist> installer\classicmac_sapi5.iss
        powershell -ExecutionPolicy Bypass -File tools\check_installer_a11y.ps1

    Every control on every page is queried through MSAA (oleacc), not UI Automation:
    PowerShell's UIA client reports almost every Win32 control as a generic Pane, which
    makes it useless for judging whether a control is actually exposed. oleacc reports what
    NVDA and JAWS see.

    A control is flagged when it is focusable but has no accessible name, since that is the
    case a screen reader announces as nothing but its type.

    The wizard is driven forward through MSAA's own default action - the same path a
    keyboard user takes - and cancelled before anything is installed.
#>
param(
    [string]$Probe = (Join-Path (Split-Path -Parent $PSScriptRoot) 'dist\ClassicMacVoices_AccessibilityProbe.exe'),
    [int]$MaxPages = 8
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $Probe)) { throw "probe installer not found: $Probe" }

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

public static class Msaa
{
    [DllImport("oleacc.dll")]
    public static extern int AccessibleObjectFromWindow(IntPtr hwnd, uint id, ref Guid iid,
        [MarshalAs(UnmanagedType.IUnknown)] out object ppv);

    public delegate bool EnumProc(IntPtr hwnd, IntPtr lparam);

    [DllImport("user32.dll")]
    public static extern bool EnumChildWindows(IntPtr parent, EnumProc cb, IntPtr lparam);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetClassNameW(IntPtr hwnd, StringBuilder buf, int max);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetWindowTextW(IntPtr hwnd, StringBuilder buf, int max);
    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")]
    public static extern bool IsWindowEnabled(IntPtr hwnd);
    [DllImport("user32.dll")]
    public static extern bool EnumWindows(EnumProc cb, IntPtr lparam);

    // FindWindow is unreliable here because the wizard belongs to a temporary copy of the
    // installer; enumerating and matching the class is not.
    public static IntPtr FindByClass(string wanted)
    {
        IntPtr hit = IntPtr.Zero;
        EnumWindows((h, l) => {
            if (!IsWindowVisible(h)) return true;
            if (ClassOf(h) == wanted) { hit = h; return false; }
            return true;
        }, IntPtr.Zero);
        return hit;
    }

    // IAccessible derives from IDispatch, so asking for IID_IDispatch hands back an object
    // PowerShell can late-bind against without an interop assembly.
    public static readonly Guid IID_IDispatch = new Guid("00020400-0000-0000-C000-000000000046");
    public const uint OBJID_CLIENT = 0xFFFFFFFC;

    public static List<IntPtr> Children(IntPtr parent)
    {
        var found = new List<IntPtr>();
        EnumChildWindows(parent, (h, l) => { found.Add(h); return true; }, IntPtr.Zero);
        return found;
    }

    public static string ClassOf(IntPtr hwnd)
    {
        var sb = new StringBuilder(256);
        GetClassNameW(hwnd, sb, sb.Capacity);
        return sb.ToString();
    }

    public static string TextOf(IntPtr hwnd)
    {
        var sb = new StringBuilder(1024);
        GetWindowTextW(hwnd, sb, sb.Capacity);
        return sb.ToString();
    }

    public static object Accessible(IntPtr hwnd)
    {
        object acc;
        Guid iid = IID_IDispatch;
        if (AccessibleObjectFromWindow(hwnd, OBJID_CLIENT, ref iid, out acc) == 0) return acc;
        return null;
    }
}
'@

# MSAA ROLE_SYSTEM_* values, which are defined in hex in oleacc.h and are easy to
# transcribe wrongly. These are the decimal equivalents.
$roleNames = @{
    3  = 'scroll bar';  9  = 'window';      10 = 'client';      20 = 'grouping'
    21 = 'separator';   33 = 'list';        34 = 'list item';   35 = 'outline'
    36 = 'outline item'; 41 = 'static text'; 42 = 'editable text'; 43 = 'push button'
    44 = 'check box';   45 = 'radio button'; 46 = 'combo box';  47 = 'drop list'
    48 = 'progress bar'; 50 = 'hotkey field'; 51 = 'slider';    52 = 'spin box'
}

# Container panels legitimately have no name of their own; a screen reader never lands on
# them. Only real controls are held to the naming rule.
$containerRoles = @(9, 10, 20, 21)

function Get-AccName($acc) {
    try { return [string]$acc.accName(0) } catch { return '' }
}
function Get-AccRole($acc) {
    try { return [int]$acc.accRole(0) } catch { return -1 }
}
function Get-AccState($acc) {
    try { return [int]$acc.accState(0) } catch { return 0 }
}

$STATE_INVISIBLE  = 0x8000
$STATE_FOCUSABLE  = 0x100000
$STATE_UNAVAILABLE = 0x1

# A stray wizard from an earlier run would be found instead of the fresh one.
Get-Process -ErrorAction SilentlyContinue |
    Where-Object { $_.ProcessName -match 'Infovox330SAPI5_Accessibility' } |
    Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 400

Write-Host "Launching $Probe"
$proc = Start-Process -FilePath $Probe -PassThru
# Inno Setup relaunches itself from a temporary copy, so the window belongs to a different
# process than the one just started. Find it by window class instead.
$deadline = (Get-Date).AddSeconds(25)
$hwnd = [IntPtr]::Zero
while ((Get-Date) -lt $deadline) {
    $hwnd = [Msaa]::FindByClass('TWizardForm')
    if ($hwnd -ne [IntPtr]::Zero) { break }
    Start-Sleep -Milliseconds 200
}
if ($hwnd -eq [IntPtr]::Zero) { throw 'the wizard window never appeared' }

Write-Host "Wizard window: '$([Msaa]::TextOf($hwnd))'"
Write-Host ""

$unnamed = 0
$total = 0
$pagesSeen = 0

for ($page = 1; $page -le $MaxPages; $page++) {
    Start-Sleep -Milliseconds 700
    $title = [Msaa]::TextOf($hwnd)

    # The page's own heading is the first visible static text on the form.
    $controls = @()
    foreach ($child in [Msaa]::Children($hwnd)) {
        if (-not [Msaa]::IsWindowVisible($child)) { continue }
        $acc = [Msaa]::Accessible($child)
        if ($null -eq $acc) { continue }
        $state = Get-AccState $acc
        if ($state -band $STATE_INVISIBLE) { continue }
        $role = Get-AccRole $acc
        $name = Get-AccName $acc
        $cls = [Msaa]::ClassOf($child)
        if ([string]::IsNullOrWhiteSpace($name)) { $name = [Msaa]::TextOf($child) }
        $controls += [pscustomobject]@{
            Class     = $cls
            RawRole   = $role
            RawState  = $state
            Role      = if ($roleNames.ContainsKey($role)) { $roleNames[$role] } else { "role $role" }
            Name      = ($name -replace '\s+', ' ').Trim()
            Focusable = [bool]($state -band $STATE_FOCUSABLE)
            Enabled   = -not [bool]($state -band $STATE_UNAVAILABLE)
        }
    }

    $pagesSeen++
    $heading = ($controls | Where-Object { $_.Role -eq 'static text' -and $_.Name } | Select-Object -First 1).Name
    Write-Host "--- Page $page : $heading" -ForegroundColor Cyan
    Write-Host "    window title: $title"

    foreach ($c in $controls) {
        $total++
        $flag = ''
        if ($c.Focusable -and $c.Enabled -and -not $c.Name -and
            $containerRoles -notcontains $c.RawRole) {
            $flag = '   <== FOCUSABLE WITH NO ACCESSIBLE NAME'
            $unnamed++
        }
        $marker = if ($c.Focusable) { '[tab]' } else { '     ' }
        $short = if ($c.Name.Length -gt 84) { $c.Name.Substring(0, 81) + '...' } else { $c.Name }
        Write-Host ("    {0} {1,-18} {2,-13} {3}{4}" -f $marker, $c.Class, $c.Role, $short, $flag)
    }
    Write-Host ""

    # Stop at the last page before anything is written. The page heading is checked as well
    # as the button caption, because a caption read back through MSAA can lag a page change
    # by a moment - and trusting it alone once let this script install the probe.
    if ($heading -match 'Ready to Install') {
        Write-Host "Reached the last page before installing; stopping here." -ForegroundColor Yellow
        break
    }

    # Move on through the wizard's own Next button, the way a keyboard user would.
    $next = $controls | Where-Object { $_.Role -eq 'push button' -and $_.Name -match 'Next|Install' } | Select-Object -First 1
    if (-not $next) { break }
    if ($next.Name -match 'Install') {
        Write-Host "Reached the Install button; stopping before anything is written." -ForegroundColor Yellow
        break
    }

    $clicked = $false
    foreach ($child in [Msaa]::Children($hwnd)) {
        if (-not [Msaa]::IsWindowVisible($child)) { continue }
        $acc = [Msaa]::Accessible($child)
        if ($null -eq $acc) { continue }
        if ((Get-AccRole $acc) -ne 43) { continue }   # ROLE_SYSTEM_PUSHBUTTON
        if ((Get-AccName $acc) -notmatch 'Next') { continue }
        try { $acc.accDoDefaultAction(0); $clicked = $true } catch { }
        break
    }
    if (-not $clicked) { break }
}

Get-Process -Name 'ClassicMacVoices_AccessibilityProbe','_isetup' -ErrorAction SilentlyContinue |
    Stop-Process -Force -ErrorAction SilentlyContinue

# Belt and braces: if a walk ever does complete an installation, undo it, so the probe can
# never leave a half-real "Infovox 330 SAPI 5" behind on the machine.
$probeUninstaller = Join-Path $env:LOCALAPPDATA 'Programs\Infovox330SAPI5\unins000.exe'
if (Test-Path $probeUninstaller) {
    Write-Host 'The probe had installed itself; removing it.' -ForegroundColor Yellow
    Start-Process $probeUninstaller -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART' -Wait
    Start-Sleep -Seconds 2
}
if (-not $proc.HasExited) { $proc | Stop-Process -Force -ErrorAction SilentlyContinue }

Write-Host "================================================================"
Write-Host "$pagesSeen pages, $total visible controls, $unnamed focusable without a name"
if ($unnamed -eq 0) {
    Write-Host "Every focusable control reports a name to MSAA." -ForegroundColor Green
    exit 0
} else {
    Write-Host "Some controls would be announced as their type only." -ForegroundColor Red
    exit 1
}

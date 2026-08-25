<#
.SYNOPSIS
    Reads every control in the settings utility through MSAA and reports what a screen
    reader would be told.

.DESCRIPTION
        powershell -ExecutionPolicy Bypass -File tools\check_config_a11y.ps1

    Queries oleacc rather than UI Automation, because PowerShell's UIA client reports
    almost every Win32 control as a generic Pane; oleacc reports what NVDA and JAWS see.
    A control is flagged when it is focusable but has no accessible name, since that is
    the case a screen reader announces as nothing but its type.
#>
param(
    [string]$Exe = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build_x86\bin\Release\ClassicMacConfig.exe'),
    [string]$Title = 'Classic Mac Voices settings'
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path $Exe)) { throw "not found: $Exe - build first" }

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

public static class Msaa2
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
    public static extern bool EnumWindows(EnumProc cb, IntPtr lparam);
    [DllImport("user32.dll")]
    public static extern bool PostMessageW(IntPtr hwnd, uint msg, IntPtr w, IntPtr l);

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

    public static IntPtr FindByTitle(string wanted)
    {
        IntPtr hit = IntPtr.Zero;
        EnumWindows((h, l) => {
            if (!IsWindowVisible(h)) return true;
            if (TextOf(h) == wanted) { hit = h; return false; }
            return true;
        }, IntPtr.Zero);
        return hit;
    }

    public static readonly Guid IID_IDispatch = new Guid("00020400-0000-0000-C000-000000000046");
    public const uint OBJID_CLIENT = 0xFFFFFFFC;

    public static List<IntPtr> Children(IntPtr parent)
    {
        var found = new List<IntPtr>();
        EnumChildWindows(parent, (h, l) => { found.Add(h); return true; }, IntPtr.Zero);
        return found;
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

$roleNames = @{
    3  = 'scroll bar';  9  = 'window';      10 = 'client';      20 = 'grouping'
    21 = 'separator';   33 = 'list';        34 = 'list item';   41 = 'static text'
    42 = 'editable text'; 43 = 'push button'; 44 = 'check box'; 45 = 'radio button'
    46 = 'combo box';   47 = 'drop list';   51 = 'slider';      52 = 'spin box'
}
$containerRoles = @(9, 10, 20, 21)

function Get-AccName($acc) { try { return [string]$acc.accName(0) } catch { return '' } }
function Get-AccRole($acc) { try { return [int]$acc.accRole(0) } catch { return -1 } }
function Get-AccState($acc) { try { return [int]$acc.accState(0) } catch { return 0 } }

$STATE_INVISIBLE = 0x8000
$STATE_FOCUSABLE = 0x100000
$STATE_UNAVAILABLE = 0x1

Write-Host "Launching $Exe"
$proc = Start-Process -FilePath $Exe -PassThru

$deadline = (Get-Date).AddSeconds(15)
$hwnd = [IntPtr]::Zero
while ((Get-Date) -lt $deadline) {
    $hwnd = [Msaa2]::FindByTitle($Title)
    if ($hwnd -ne [IntPtr]::Zero) { break }
    Start-Sleep -Milliseconds 200
}
if ($hwnd -eq [IntPtr]::Zero) { throw 'the settings window never appeared' }
Start-Sleep -Milliseconds 500

$unnamed = 0
$total = 0
foreach ($child in [Msaa2]::Children($hwnd)) {
    if (-not [Msaa2]::IsWindowVisible($child)) { continue }
    $acc = [Msaa2]::Accessible($child)
    if ($null -eq $acc) { continue }
    $state = Get-AccState $acc
    if ($state -band $STATE_INVISIBLE) { continue }
    $role = Get-AccRole $acc
    $name = Get-AccName $acc
    $cls = [Msaa2]::ClassOf($child)
    if ([string]::IsNullOrWhiteSpace($name)) { $name = [Msaa2]::TextOf($child) }
    $name = ($name -replace '\s+', ' ').Trim()
    $focusable = [bool]($state -band $STATE_FOCUSABLE)
    $enabled = -not [bool]($state -band $STATE_UNAVAILABLE)
    $roleText = if ($roleNames.ContainsKey($role)) { $roleNames[$role] } else { "role $role" }

    $total++
    $flag = ''
    if ($focusable -and $enabled -and -not $name -and $containerRoles -notcontains $role) {
        $flag = '   <== FOCUSABLE WITH NO ACCESSIBLE NAME'
        $unnamed++
    }
    $tab = if ($focusable) { '[tab]' } else { '     ' }
    Write-Host ("    {0} {1,-18} {2,-13} {3}{4}" -f $tab, $cls, $roleText, $name, $flag)
}

# WM_CLOSE; the utility saves on close.
[Msaa2]::PostMessageW($hwnd, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null

Write-Host ""
Write-Host "================================================================"
Write-Host "$total visible controls, $unnamed focusable without a name"
if ($unnamed -eq 0) {
    Write-Host 'Every focusable control reports a name to MSAA.'
    exit 0
}
exit 1

# -*- coding: utf-8 -*-
"""Render a sample WAV for every Leopard voice, straight through the host.

No SAPI4, no SAPI5, no registry: this speaks the same framed-stdio protocol
the NVDA driver uses -- `panthera_host.exe --serve <MacinTalk>
<SpeechDictionary> <voicesdir>` -- and is the proof that the engine runs
standalone before any COM wrapper is built on top of it.

Protocol (from bin/_panthera/pantheradriver.py):
    request:  <IiiIII  = magic 'TGR3', wpm, pitch (tenths of a semitone
              offset), reserved 0, len(voice utf-8), len(text mac_roman)
              followed by the two byte strings.
    response: <IiI     = magic 'TGRS', OSErr status, frame count,
              followed by frames*2 bytes of 16-bit mono PCM at 22050 Hz.
"""
import os
import struct
import subprocess
import sys
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BIN = os.path.join(ROOT, "bin")
TREE = os.path.join(BIN, "leopard")
HOST = os.path.join(BIN, "_panthera", "panthera_host.exe")

MT = os.path.join(TREE, "Speech", "Synthesizers",
                  "MacinTalk.SpeechSynthesizer", "Contents", "MacOS",
                  "MacinTalk")
SD = os.path.join(TREE, "SpeechDictionary.framework", "Versions", "A",
                  "SpeechDictionary")
VOICES = os.path.join(TREE, "Speech", "Voices")

REQ_MAGIC = 0x54475233   # 'TGR3'
RSP_MAGIC = 0x54475253   # 'TGRS'
OUT_RATE = 22050

#: Leopard's measured per-voice levels, from pantheradriver.VOLUME_NORM_LEOPARD,
#: so the samples sit at roughly one loudness -- Alex is 8 dB quieter than
#: Bruce without it.
VOLUME_NORM = {
    "Agnes": 1.00, "Albert": 1.70, "Alex": 1.80, "BadNews": 1.80,
    "Bahh": 1.70, "Bells": 1.70, "Boing": 1.70, "Bruce": 1.00,
    "Bubbles": 1.70, "Cellos": 1.70, "Deranged": 1.70, "Fred": 1.80,
    "GoodNews": 1.80, "Hysterical": 1.70, "Junior": 1.80, "Kathy": 1.73,
    "Organ": 1.70, "Princess": 1.70, "Ralph": 1.70, "Trinoids": 1.70,
    "Vicki": 1.20, "Victoria": 1.00, "Whisper": 1.80, "Zarvox": 1.70,
}

#: Concatenative voices first, like the NVDA driver orders its list.
ENGINE_ORDER = {"meow": 0, "gala": 1, "mtk3": 2}


def read_voices():
    """-> [(bundleName, engine)], routed by the VoiceDescription creator."""
    out = []
    for entry in sorted(os.listdir(VOICES)):
        if not entry.endswith(".SpeechVoice"):
            continue
        desc = os.path.join(VOICES, entry, "Contents", "Resources",
                            "VoiceDescription")
        try:
            with open(desc, "rb") as f:
                head = f.read(80)
        except OSError:
            continue
        if len(head) < 80:
            continue
        engine = head[4:8].decode("latin-1")
        out.append((entry[:-len(".SpeechVoice")], engine))
    out.sort(key=lambda v: (ENGINE_ORDER.get(v[1], 3), v[0].lower()))
    return out


def read_exactly(stream, n):
    buf = b""
    while len(buf) < n:
        piece = stream.read(n - len(buf))
        if not piece:
            raise IOError("host closed the pipe after %d of %d bytes"
                          % (len(buf), n))
        buf += piece
    return buf


def render(proc, voice, text, wpm=180, pitch=0):
    """-> PCM bytes for one utterance."""
    volm = VOLUME_NORM.get(voice, 1.0)
    text = "[[volm %.3f]]%s" % (volm, text)
    v = voice.encode("utf-8")
    t = text.encode("mac_roman", "replace")
    proc.stdin.write(struct.pack("<IiiIII", REQ_MAGIC, wpm, pitch,
                                 0, len(v), len(t)) + v + t)
    proc.stdin.flush()
    magic, status, nframes = struct.unpack(
        "<IiI", read_exactly(proc.stdout, 12))
    if magic != RSP_MAGIC:
        raise IOError("bad response magic %08x" % magic)
    pcm = read_exactly(proc.stdout, nframes * 2)
    if status:
        print("  OSErr %d for %s" % (status, voice))
    return pcm


def write_wav(path, pcm):
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(OUT_RATE)
        w.writeframes(pcm)


def main():
    outdir = os.path.join(ROOT, "samples")
    os.makedirs(outdir, exist_ok=True)
    voices = read_voices()
    print("Found %d voices: %s" % (
        len(voices), ", ".join("%s (%s)" % v for v in voices)))

    proc = subprocess.Popen(
        [HOST, "--serve", MT, SD, VOICES],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL)
    all_pcm = []
    silence = b"\x00\x00" * int(OUT_RATE * 0.4)
    try:
        for i, (voice, engine) in enumerate(voices, start=1):
            text = ("Hello, my name is %s. I am a classic Macintosh voice "
                    "from Mac OS X 10.5 Leopard, speaking on Windows."
                    % voice)
            pcm = render(proc, voice, text)
            secs = len(pcm) / 2.0 / OUT_RATE
            name = "%02d_en-US_%s.wav" % (i, voice)
            write_wav(os.path.join(outdir, name), pcm)
            print("  %-28s %6.2f s  (%s)" % (name, secs, engine))
            if not pcm:
                print("  !! %s rendered NO AUDIO" % voice)
            all_pcm.append(pcm)
            all_pcm.append(silence)
        write_wav(os.path.join(outdir, "00_all_voices.wav"),
                  b"".join(all_pcm))
        print("Wrote %d per-voice samples plus 00_all_voices.wav to %s"
              % (len(voices), outdir))
    finally:
        try:
            proc.stdin.close()
            proc.wait(timeout=3)
        except Exception:
            proc.kill()


if __name__ == "__main__":
    sys.exit(main())

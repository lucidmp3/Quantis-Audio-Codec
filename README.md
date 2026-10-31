# Quantis Audio Codec 🎵

*Engineered by Lucid Audio*

Quantis is a custom, lightweight audio codec and player ecosystem designed from the ground up in modern C++ and JavaScript. It features a proprietary .qnt binary format that achieves 50% compression on standard uncompressed PCM audio with near-transparent perceptual quality using block-based Adaptive DPCM (ADPCM).

## 🚀 Features

Custom ADPCM Compression: Shrinks standard 16-bit WAV files by 50% using dynamic scale factor calculation over 500-sample blocks.

Built-in DSP Auto-Converter: Automatically down-samples 24-bit integer and 32-bit floating-point WAV files to 16-bit in memory before compression to prevent hardware crashing.

Modern Windows Player: A standalone, MCI-powered Windows desktop app featuring:

A sleek, custom-rendered GUI that syncs with your Windows Light/Dark mode.

Live trackbar slider, Play/Pause, and Seek controls.

Drag-and-drop support.

Native OS Integration: The included installer registers .qnt files in the Windows Registry so they share the app icon and open instantly when double-clicked.

Web Converter: A 100% offline HTML/JS web application that mirrors the C++ encoding/decoding engine perfectly.

## 📥 Installation (Windows)

Navigate to the Releases tab on this repository.

Download the latest Quantis_Setup_v1.1.2.exe.

Double-click to install.

Note: Because this is an independent, open-source project without a paid corporate certificate, Windows SmartScreen will show a blue warning. Click More Info -> Run anyway to proceed.

Enjoy native .qnt playback!

## 🛠️ Building from Source (C++)

If you want to compile the desktop application yourself, you will need Visual Studio (MSVC) and CMake.

# Clone the repository
git clone https://github.com/yourusername/Quantis-Audio-Codec.git

# Generate build files
cmake -B build

# Build in Release mode (Standalone, statically linked)
cmake --build build --config Release


## 🧠 How the Format Works (The Tech Specs)

Every .qnt file is wrapped in a custom 16-byte binary header:

[4 bytes] Magic Number (QNT1)

[4 bytes] Sample Rate (e.g., 44100)

[2 bytes] Channels (1 for Mono, 2 for Stereo)

[2 bytes] Bit Depth (Always 16)

[4 bytes] Data Size

The audio stream is divided into frames of 500 samples. Each frame begins with an 8-bit scale factor (info-byte), followed by the delta-encoded, quantized audio data. This completely eliminates static noise during quiet passages (psychoacoustic masking) while preventing clipping during loud transients.

Created by Lucid Audio

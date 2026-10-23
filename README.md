# bytebeat-cli

A bytebeat command-line player/generator supporting unsigned, signed, floatbeat,
and funcbeat formulas. It evaluates formulas with QuickJS and can write the
generated audio as WAV.

## Build

You need a C compiler.

- Linux: `./build-linux.sh`
- macOS: `./build-macos.sh`
- Windows: run `.\build.ps1` in PowerShell. This script expects Clang, Visual
  Studio Build Tools, and the Windows SDK.

On Windows, `.\build-gui.ps1` builds the GUI version and also requires the
Windows SDK and Visual Studio Build Tools.

## Usage

```sh
./bytebeat --mode unsigned --duration 10 --output song.wav "t*(t>>5|t>>8)"
```

Use `./bytebeat --help` for the complete list of options. Without an explicit
mode, the program uses unsigned bytebeat. WAV data is written to standard output
unless `--output` is specified.

## Credits and third-party software

The JavaScript engine is QuickJS by Fabrice Bellard and Charlie Gordon. Its
bundled source files and license are documented in
[THIRD_PARTY_NOTICES.md](./THIRD_PARTY_NOTICES.md).

The GUI layout was inspired by SthephanShi's
[dollchan.net](https://dollchan.net/bytebeat).

## License

The MIT license in [LICENSE](./LICENSE) applies to the original bytebeat-cli
project code. QuickJS is separately provided under its own MIT license; see
[THIRD_PARTY_NOTICES.md](./THIRD_PARTY_NOTICES.md).

<p float="left">
    <img src="web/assets/resources/WhiteKing.png" width=40 />
    <img src="web/assets/resources/WhiteQueen.png" width=40 />
    <img src="web/assets/resources/WhiteRook.png" width=40 />
    <img src="web/assets/resources/WhiteBishop.png" width=40 />
    <img src="web/assets/resources/WhiteKnight.png" width=40 />
    <img src="web/assets/resources/WhitePawn.png" width=40 />
    <img src="web/assets/resources/BlackKing.png" width=40 />
    <img src="web/assets/resources/BlackQueen.png" width=40 />
    <img src="web/assets/resources/BlackRook.png" width=40 />
    <img src="web/assets/resources/BlackBishop.png" width=40 />
    <img src="web/assets/resources/BlackKnight.png" width=40 />
    <img src="web/assets/resources/BlackPawn.png" width=40 />
</p>

Sample web UI for the fatpup chess engine based on Emscripten (`HTML + JS + WASM`).

## Get the code

```bash
git clone https://github.com/witaly-iwanow/fatpup-wasm.git
cd fatpup-wasm
```

`fatpup` is fetched automatically via CMake `FetchContent` (pinned commit).

## Build (macOS/Linux)

Install and activate Emscripten SDK first, then:

```bash
emcmake cmake -S . -B build
cmake --build build -j
```

## Build (Windows)

Use an Emscripten-enabled shell (`emsdk_env.bat`) and run:

```bat
emcmake cmake -S . -B build
cmake --build build -j
```

## Run

Serve the output directory:

```bash
cd build/web
python3 -m http.server 8080
```

Open `http://localhost:8080`.

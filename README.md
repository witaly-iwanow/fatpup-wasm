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

## Build

Install and activate Emscripten SDK first (on Windows use an Emscripten-enabled shell, e.g. `emsdk_env.bat`), then:

```bat
emcmake cmake -S . -B _build
cmake --build _build -j
```

## Run

Serve the GitHub Pages directory:

```bash
cd docs
python3 -m http.server 8080
```

Open `http://localhost:8080`.

![Screenshot](screenshots/game.png)

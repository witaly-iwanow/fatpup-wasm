(() => {
  const SPRITES = {
    K: "assets/resources/WhiteKing.png",
    Q: "assets/resources/WhiteQueen.png",
    R: "assets/resources/WhiteRook.png",
    B: "assets/resources/WhiteBishop.png",
    N: "assets/resources/WhiteKnight.png",
    P: "assets/resources/WhitePawn.png",
    k: "assets/resources/BlackKing.png",
    q: "assets/resources/BlackQueen.png",
    r: "assets/resources/BlackRook.png",
    b: "assets/resources/BlackBishop.png",
    n: "assets/resources/BlackKnight.png",
    p: "assets/resources/BlackPawn.png"
  };
  const MOVE_TEXT_WIDTH = "0-0-0+".length;

  const state = {
    ready: false,
    data: null,
    selectedSquare: "",
    gameOverToastTimer: null,
    lastGameOverMessage: "",
    fpInit: null,
    fpCommand: null,
    fpGetState: null
  };

  const dom = {
    board: document.getElementById("board"),
    gameOverToast: document.getElementById("game-over-toast"),
    moveHistory: document.getElementById("move-history"),
    btnBack: document.getElementById("btn-back"),
    btnFlip: document.getElementById("btn-flip"),
    btnLoadFen: document.getElementById("btn-load-fen"),
    btnRestart: document.getElementById("btn-restart")
  };

  function refreshState() {
    if (!state.ready) {
      return;
    }

    let parsed = null;
    try {
      parsed = JSON.parse(state.fpGetState());
    } catch (error) {
      console.error(`State parse error: ${String(error)}`);
      return;
    }

    if (parsed.error) {
      console.error(parsed.error);
      return;
    }

    const previousData = state.data;
    state.data = parsed;
    renderAll();
    maybeShowGameOverToast(previousData, parsed);
  }

  function displayToWhiteCoordinates(displayRow, displayCol) {
    if (!state.data || state.data.userPlaysWhite) {
      return { whiteRow: displayRow, whiteCol: displayCol };
    }
    return { whiteRow: 7 - displayRow, whiteCol: 7 - displayCol };
  }

  function cellCoord(displayRow, displayCol) {
    const { whiteRow, whiteCol } = displayToWhiteCoordinates(displayRow, displayCol);
    const file = String.fromCharCode(97 + whiteCol);
    const rank = String.fromCharCode(56 - whiteRow);
    return `${file}${rank}`;
  }

  function boardIndexFromCoord(coord) {
    if (!coord || coord.length !== 2) {
      return -1;
    }
    const file = coord.charCodeAt(0) - 97;
    const rank = coord.charCodeAt(1) - 48;
    if (file < 0 || file > 7 || rank < 1 || rank > 8) {
      return -1;
    }
    const whiteRow = 8 - rank;
    return whiteRow * 8 + file;
  }

  function pieceAtCoord(coord) {
    if (!state.data || !state.data.board) {
      return ".";
    }
    const idx = boardIndexFromCoord(coord);
    if (idx < 0 || idx >= state.data.board.length) {
      return ".";
    }
    return state.data.board[idx];
  }

  function isUserPiece(pieceChar) {
    if (!pieceChar || pieceChar === "." || !state.data) {
      return false;
    }
    const pieceIsWhite = pieceChar === pieceChar.toUpperCase();
    const sideToMoveWhite = !!state.data.whiteTurn;
    const userSideWhite = !!state.data.userPlaysWhite;
    return pieceIsWhite === sideToMoveWhite && sideToMoveWhite === userSideWhite;
  }

  function maybePromotionSuffix(fromCoord, toCoord) {
    const piece = pieceAtCoord(fromCoord);
    if (!piece || piece.toLowerCase() !== "p") {
      return "";
    }
    const targetRank = toCoord[1];
    if ((piece === "P" && targetRank === "8") || (piece === "p" && targetRank === "1")) {
      return "q";
    }
    return "";
  }

  function runCommand(command) {
    if (!state.ready) {
      return "";
    }

    state.selectedSquare = "";
    const result = String(state.fpCommand(command) || "");
    refreshState();
    return result;
  }

  function applyFen(fen, onError) {
    const trimmedFen = String(fen || "").trim();
    if (!trimmedFen) {
      if (onError) {
        onError("FEN cannot be empty.");
      }
      return false;
    }

    const result = runCommand(`fen ${trimmedFen}`);
    if (result.includes("Invalid FEN.")) {
      if (onError) {
        onError(result);
      }
      return false;
    }

    return true;
  }

  function loadFen() {
    const currentFen = state.data ? String(state.data.fen || "") : "";
    const input = window.prompt("Paste a FEN string.", currentFen);
    if (input === null) {
      return;
    }

    applyFen(input, (message) => window.alert(message));
  }

  function onSquareClick(displayRow, displayCol) {
    if (!state.data) {
      return;
    }

    if (state.data.gameOver || !state.data.userToMove) {
      return;
    }

    const coord = cellCoord(displayRow, displayCol);

    if (!state.selectedSquare) {
      const piece = pieceAtCoord(coord);
      if (!isUserPiece(piece)) {
        return;
      }
      state.selectedSquare = coord;
      renderBoard();
      return;
    }

    if (coord === state.selectedSquare) {
      state.selectedSquare = "";
      renderBoard();
      return;
    }

    const move = `${state.selectedSquare}${coord}${maybePromotionSuffix(state.selectedSquare, coord)}`;
    state.selectedSquare = "";
    runCommand(move);
  }

  function gameResultText(data) {
    if (!data || !data.gameOver) {
      return "";
    }

    const status = String(data.status || "").toLowerCase();
    if (status.includes("stalemate")) {
      return "Draw";
    }
    if (status.includes("checkmate")) {
      return data.whiteTurn ? "Black won" : "White won";
    }

    return "Draw";
  }

  function hideGameOverToast() {
    if (state.gameOverToastTimer !== null) {
      window.clearTimeout(state.gameOverToastTimer);
      state.gameOverToastTimer = null;
    }
    dom.gameOverToast.classList.remove("visible");
  }

  function showGameOverToast(message) {
    hideGameOverToast();
    dom.gameOverToast.textContent = message;
    dom.gameOverToast.classList.add("visible");
    state.gameOverToastTimer = window.setTimeout(() => {
      dom.gameOverToast.classList.remove("visible");
      state.gameOverToastTimer = null;
    }, 7800);
  }

  function maybeShowGameOverToast(previousData, nextData) {
    const message = gameResultText(nextData);
    if (!message) {
      state.lastGameOverMessage = "";
      hideGameOverToast();
      return;
    }

    const previousMessage = gameResultText(previousData);
    if (message === previousMessage && message === state.lastGameOverMessage) {
      return;
    }

    state.lastGameOverMessage = message;
    showGameOverToast(message);
  }

  function ensureBoardSkeleton() {
    if (dom.board.childElementCount === 64) {
      return;
    }

    dom.board.innerHTML = "";
    for (let row = 0; row < 8; row += 1) {
      for (let col = 0; col < 8; col += 1) {
        const cell = document.createElement("button");
        cell.type = "button";
        cell.className = `square ${((row + col) & 1) ? "dark" : "light"}`;
        cell.dataset.row = String(row);
        cell.dataset.col = String(col);
        cell.addEventListener("click", () => onSquareClick(row, col));
        dom.board.appendChild(cell);
      }
    }
  }

  function renderBoard() {
    if (!state.data) {
      return;
    }

    ensureBoardSkeleton();

    const lastMove = state.data.lastMove || "";
    const lastSrc = lastMove.length >= 4 ? lastMove.slice(0, 2) : "";
    const lastDst = lastMove.length >= 4 ? lastMove.slice(2, 4) : "";

    for (const cell of dom.board.children) {
      const row = Number(cell.dataset.row);
      const col = Number(cell.dataset.col);
      const coord = cellCoord(row, col);
      const idx = boardIndexFromCoord(coord);
      const piece = idx >= 0 ? state.data.board[idx] : ".";

      cell.classList.remove("selected", "last-move");
      if (coord === state.selectedSquare) {
        cell.classList.add("selected");
      }
      if (coord === lastSrc || coord === lastDst) {
        cell.classList.add("last-move");
      }

      cell.innerHTML = (piece && piece !== "." && SPRITES[piece])
        ? `<img src="${SPRITES[piece]}" alt="${piece}">`
        : "";
    }
  }

  function renderMovePanel() {
    if (!state.data) {
      return;
    }

    const formatMoveNumber = (moveNumber) => String(`${moveNumber}.`).padStart(3, " ");
    const formatMoveText = (moveText) => String(moveText || "").padEnd(MOVE_TEXT_WIDTH, " ");

    const white = state.data.whiteMoves || [];
    const black = state.data.blackMoves || [];
    const lines = [];
    const startFullMoveNumber = Number(state.data.startFullMoveNumber) || 1;
    const startWhiteTurn = state.data.startWhiteTurn !== false;

    if (startWhiteTurn) {
      const rows = Math.max(white.length, black.length);
      for (let i = 0; i < rows; i += 1) {
        const parts = [formatMoveNumber(startFullMoveNumber + i)];
        if (white[i]) {
          parts.push(formatMoveText(white[i]));
        }
        if (black[i]) {
          parts.push(formatMoveText(black[i]));
        }
        lines.push(parts.join(" "));
      }
    } else {
      if (black[0]) {
        lines.push(`${formatMoveNumber(startFullMoveNumber)} ${formatMoveText("..")} ${formatMoveText(black[0])}`);
      }

      const rows = Math.max(white.length, Math.max(0, black.length - 1));
      for (let i = 0; i < rows; i += 1) {
        const parts = [formatMoveNumber(startFullMoveNumber + 1 + i)];
        if (white[i]) {
          parts.push(formatMoveText(white[i]));
        }
        if (black[i + 1]) {
          parts.push(formatMoveText(black[i + 1]));
        }
        lines.push(parts.join(" "));
      }
    }

    const result = gameResultText(state.data);
    if (result) {
      if (lines.length > 0) {
        lines.push("");
      }
      lines.push(result);
    }

    dom.moveHistory.textContent = lines.join("\n");
  }

  function renderAll() {
    renderBoard();
    renderMovePanel();
  }

  function wireControls() {
    dom.btnBack.addEventListener("click", () => runCommand("back"));
    dom.btnFlip.addEventListener("click", () => runCommand("flip"));
    dom.btnLoadFen.addEventListener("click", loadFen);
    dom.btnRestart.addEventListener("click", () => runCommand("restart"));
  }

  function boot() {
    state.fpInit = Module.cwrap("fp_init", "string", []);
    state.fpCommand = Module.cwrap("fp_command", "string", ["string"]);
    state.fpGetState = Module.cwrap("fp_get_state_json", "string", []);
    state.ready = true;

    ensureBoardSkeleton();
    wireControls();

    state.fpInit();

    refreshState();
  }

  window.Module = {
    onRuntimeInitialized: boot
  };
})();

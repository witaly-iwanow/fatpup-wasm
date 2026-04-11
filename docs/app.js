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
  const MOVE_ANIMATION_MS = 140;
  const PIECE_SCALE = 0.92;
  const PROMOTION_OPTIONS = [
    { suffix: "q", label: "Queen", whitePiece: "Q", blackPiece: "q" },
    { suffix: "r", label: "Rook", whitePiece: "R", blackPiece: "r" },
    { suffix: "b", label: "Bishop", whitePiece: "B", blackPiece: "b" },
    { suffix: "n", label: "Knight", whitePiece: "N", blackPiece: "n" }
  ];

  const state = {
    ready: false,
    data: null,
    selectedSquare: "",
    hiddenPieceCoord: "",
    promotionResolve: null,
    promotionPiece: "",
    gameOverToastTimer: null,
    lastGameOverMessage: "",
    copyToastTimer: null,
    moveAnimation: null,
    fpInit: null,
    fpCommand: null,
    fpGetState: null
  };

  const dom = {
    board: document.getElementById("board"),
    boardWrap: document.querySelector(".board-wrap"),
    gameOverToast: document.getElementById("game-over-toast"),
    promotionDialog: document.getElementById("promotion-dialog"),
    promotionChoices: document.getElementById("promotion-choices"),
    promotionTitle: document.getElementById("promotion-title"),
    promotionCancel: document.getElementById("promotion-cancel"),
    moveHistory: document.getElementById("move-history"),
    btnBack: document.getElementById("btn-back"),
    btnFlip: document.getElementById("btn-flip"),
    btnCopyFen: document.getElementById("btn-copy-fen"),
    btnSetFen: document.getElementById("btn-set-fen"),
    btnCopyPgn: document.getElementById("btn-copy-pgn"),
    btnRestart: document.getElementById("btn-restart"),
    btnEngine: document.getElementById("btn-engine"),
    copyToast: document.getElementById("copy-toast")
  };

  function renderEngineButton() {
    if (!dom.btnEngine) {
      return;
    }
    const mode = state.data && state.data.engineMode === "strong" ? "strong" : "weak";
    dom.btnEngine.classList.toggle("weak", mode === "weak");
    dom.btnEngine.classList.toggle("strong", mode === "strong");
    dom.btnEngine.title = mode === "strong" ? "Strong engine (click for Weak)" : "Weak engine (click for Strong)";
  }

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

    normalizeTerminalState(parsed);

    const previousData = state.data;
    const animation = buildMoveAnimation(previousData, parsed);
    cancelMoveAnimation();
    state.data = parsed;
    state.hiddenPieceCoord = animation ? animation.toCoord : "";
    renderBoard();
    renderMovePanel();
    renderEngineButton();
    if (animation) {
      playMoveAnimation(animation);
    }
    maybeShowGameOverToast(previousData, parsed);
  }

  function onlyKingsRemain(board) {
    if (!board) {
      return false;
    }
    const pieces = String(board).replace(/\./g, "");
    return pieces.length === 2 && pieces.includes("K") && pieces.includes("k");
  }

  function normalizeTerminalState(data) {
    if (!data || !onlyKingsRemain(data.board)) {
      return;
    }

    data.gameOver = true;
    data.status = "Draw.";
    data.userToMove = false;
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
    return pieceAtCoordInBoard(state.data && state.data.board, coord);
  }

  function pieceAtCoordInBoard(board, coord) {
    if (!board) {
      return ".";
    }
    const idx = boardIndexFromCoord(coord);
    return idx >= 0 && idx < board.length ? board[idx] : ".";
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

  function needsPromotion(piece, toCoord) {
    if (!piece || piece.toLowerCase() !== "p") {
      return false;
    }
    const targetRank = toCoord[1];
    if ((piece === "P" && targetRank === "8") || (piece === "p" && targetRank === "1")) {
      return true;
    }
    return false;
  }

  function closePromotionDialog(choice) {
    const resolve = state.promotionResolve;
    state.promotionResolve = null;
    state.promotionPiece = "";
    dom.promotionDialog.classList.remove("visible");
    dom.promotionDialog.setAttribute("aria-hidden", "true");
    dom.promotionChoices.innerHTML = "";
    if (resolve) {
      resolve(choice);
    }
  }

  function openPromotionDialog(piece) {
    if (!piece) {
      return Promise.resolve("");
    }

    if (state.promotionResolve) {
      closePromotionDialog("");
    }

    state.promotionPiece = piece;
    dom.promotionTitle.textContent = "Choose a promotion piece";
    dom.promotionChoices.innerHTML = "";

    for (const option of PROMOTION_OPTIONS) {
      const button = document.createElement("button");
      const spritePiece = piece === "P" ? option.whitePiece : option.blackPiece;
      button.type = "button";
      button.className = "promotion-option";
      button.dataset.suffix = option.suffix;
      button.innerHTML = `<img src="${SPRITES[spritePiece]}" alt="${option.label}"><span>${option.label}</span>`;
      button.addEventListener("click", () => closePromotionDialog(option.suffix));
      dom.promotionChoices.appendChild(button);
    }

    dom.promotionDialog.classList.add("visible");
    dom.promotionDialog.setAttribute("aria-hidden", "false");
    const firstButton = dom.promotionChoices.querySelector("button");
    if (firstButton) {
      firstButton.focus();
    }

    return new Promise((resolve) => {
      state.promotionResolve = resolve;
    });
  }

  function runCommand(command) {
    if (!state.ready) {
      return "";
    }

    if (state.promotionResolve) {
      closePromotionDialog("");
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

  function showCopyToast(message) {
    if (state.copyToastTimer !== null) {
      window.clearTimeout(state.copyToastTimer);
      state.copyToastTimer = null;
    }
    dom.copyToast.textContent = message;
    dom.copyToast.classList.add("visible");
    state.copyToastTimer = window.setTimeout(() => {
      dom.copyToast.classList.remove("visible");
      state.copyToastTimer = null;
    }, 2000);
  }

  function fallbackCopyText(text) {
    const textarea = document.createElement("textarea");
    textarea.value = text;
    textarea.setAttribute("readonly", "");
    textarea.style.position = "fixed";
    textarea.style.top = "-1000px";
    textarea.style.opacity = "0";
    document.body.appendChild(textarea);
    textarea.select();
    let succeeded = false;
    try {
      succeeded = document.execCommand("copy");
    } catch (error) {
      succeeded = false;
    }
    document.body.removeChild(textarea);
    return succeeded;
  }

  async function copyTextToClipboard(text) {
    if (!text) {
      return false;
    }

    if (navigator.clipboard && navigator.clipboard.writeText) {
      try {
        await navigator.clipboard.writeText(text);
        return true;
      } catch (error) {
        // fall through to fallback
      }
    }

    return fallbackCopyText(text);
  }

  async function copyPgn() {
    if (!state.ready) {
      return;
    }

    const pgn = String(state.fpCommand("score") || "");
    if (await copyTextToClipboard(pgn)) {
      showCopyToast("Game score copied");
    }
  }

  async function copyFen() {
    const currentFen = state.data ? String(state.data.fen || "") : "";
    if (!currentFen) {
      return;
    }
    if (await copyTextToClipboard(currentFen)) {
      showCopyToast("FEN copied");
    }
  }

  function setFen() {
    const currentFen = state.data ? String(state.data.fen || "") : "";
    const input = window.prompt("Paste a FEN string to set the position.", currentFen);
    if (input === null) {
      return;
    }
    applyFen(input, (message) => window.alert(message));
  }

  async function onSquareClick(displayRow, displayCol) {
    if (!state.data) {
      return;
    }

    if (state.data.gameOver || !state.data.userToMove || state.promotionResolve) {
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

    const fromCoord = state.selectedSquare;
    const piece = pieceAtCoord(fromCoord);
    let promotionSuffix = "";
    if (needsPromotion(piece, coord)) {
      promotionSuffix = await openPromotionDialog(piece);
      if (!promotionSuffix) {
        return;
      }
    }

    const move = `${fromCoord}${coord}${promotionSuffix}`;
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

  function displayPositionForCoord(coord) {
    if (!coord || coord.length !== 2) {
      return null;
    }

    const whiteCol = coord.charCodeAt(0) - 97;
    const rank = coord.charCodeAt(1) - 48;
    if (whiteCol < 0 || whiteCol > 7 || rank < 1 || rank > 8) {
      return null;
    }

    const whiteRow = 8 - rank;
    if (!state.data || state.data.userPlaysWhite) {
      return { row: whiteRow, col: whiteCol };
    }
    return { row: 7 - whiteRow, col: 7 - whiteCol };
  }

  function cellElementForCoord(coord) {
    const display = displayPositionForCoord(coord);
    if (!display) {
      return null;
    }
    return dom.board.querySelector(`[data-row="${display.row}"][data-col="${display.col}"]`);
  }

  function buildMoveAnimation(previousData, nextData) {
    if (!previousData || !nextData || previousData.board === nextData.board) {
      return null;
    }

    const lastMove = String(nextData.lastMove || "");
    if (lastMove.length < 4) {
      return null;
    }

    const fromCoord = lastMove.slice(0, 2);
    const toCoord = lastMove.slice(2, 4);
    const previousBoard = String(previousData.board || "");
    const nextBoard = String(nextData.board || "");
    const fromPiece = pieceAtCoordInBoard(previousBoard, fromCoord);
    const toPiece = pieceAtCoordInBoard(nextBoard, toCoord);

    if (fromPiece === "." || toPiece === "." || pieceAtCoordInBoard(nextBoard, fromCoord) !== ".") {
      return null;
    }

    const fromIsWhite = fromPiece === fromPiece.toUpperCase();
    const toIsWhite = toPiece === toPiece.toUpperCase();
    if (fromIsWhite !== toIsWhite) {
      return null;
    }

    let diffCount = 0;
    for (let i = 0; i < Math.min(previousBoard.length, nextBoard.length); i += 1) {
      if (previousBoard[i] !== nextBoard[i]) {
        diffCount += 1;
      }
    }

    if (diffCount < 2 || diffCount > 8) {
      return null;
    }

    return { fromCoord, toCoord, piece: toPiece };
  }

  function cancelMoveAnimation() {
    if (!state.moveAnimation) {
      return;
    }

    state.moveAnimation.player.cancel();
    state.moveAnimation.overlay.remove();
    state.moveAnimation = null;
    state.hiddenPieceCoord = "";
  }

  function finishMoveAnimation(overlay) {
    if (!state.moveAnimation || state.moveAnimation.overlay !== overlay) {
      return;
    }

    overlay.remove();
    state.moveAnimation = null;
    state.hiddenPieceCoord = "";
    renderBoard();
  }

  function playMoveAnimation(animation) {
    const fromCell = cellElementForCoord(animation.fromCoord);
    const toCell = cellElementForCoord(animation.toCoord);
    const sprite = SPRITES[animation.piece];
    if (!fromCell || !toCell || !sprite) {
      state.hiddenPieceCoord = "";
      renderBoard();
      return;
    }

    const boardRect = dom.board.getBoundingClientRect();
    const fromRect = fromCell.getBoundingClientRect();
    const toRect = toCell.getBoundingClientRect();
    const pieceWidth = fromRect.width * PIECE_SCALE;
    const pieceHeight = fromRect.height * PIECE_SCALE;
    const startLeft = fromRect.left - boardRect.left + ((fromRect.width - pieceWidth) / 2);
    const startTop = fromRect.top - boardRect.top + ((fromRect.height - pieceHeight) / 2);
    const deltaX = toRect.left - fromRect.left;
    const deltaY = toRect.top - fromRect.top;

    const overlay = document.createElement("img");
    overlay.className = "moving-piece";
    overlay.src = sprite;
    overlay.alt = animation.piece;
    overlay.style.left = `${startLeft}px`;
    overlay.style.top = `${startTop}px`;
    overlay.style.width = `${pieceWidth}px`;
    overlay.style.height = `${pieceHeight}px`;
    dom.boardWrap.appendChild(overlay);

    const player = overlay.animate(
      [
        { transform: "translate(0, 0)" },
        { transform: `translate(${deltaX}px, ${deltaY}px)` }
      ],
      {
        duration: MOVE_ANIMATION_MS,
        easing: "linear"
      }
    );

    state.moveAnimation = { overlay, player };
    player.onfinish = () => finishMoveAnimation(overlay);
    player.oncancel = () => {
      if (state.moveAnimation && state.moveAnimation.overlay === overlay) {
        overlay.remove();
      }
    };
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

      cell.innerHTML = (coord !== state.hiddenPieceCoord && piece && piece !== "." && SPRITES[piece])
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
    const appendMoveLine = (lines, moveNumber, whiteMove, blackMove) => {
      const parts = [formatMoveNumber(moveNumber)];
      if (whiteMove) {
        parts.push(formatMoveText(whiteMove));
      }
      if (blackMove) {
        parts.push(formatMoveText(blackMove));
      }
      lines.push(parts.join(" "));
    };

    const white = state.data.whiteMoves || [];
    const black = state.data.blackMoves || [];
    const lines = [];
    const startFullMoveNumber = Number(state.data.startFullMoveNumber) || 1;
    const startWhiteTurn = state.data.startWhiteTurn !== false;

    if (startWhiteTurn) {
      const rows = Math.max(white.length, black.length);
      for (let i = 0; i < rows; i += 1) {
        appendMoveLine(lines, startFullMoveNumber + i, white[i], black[i]);
      }
    } else {
      if (black[0]) {
        appendMoveLine(lines, startFullMoveNumber, "..", black[0]);
      }

      const rows = Math.max(white.length, Math.max(0, black.length - 1));
      for (let i = 0; i < rows; i += 1) {
        appendMoveLine(lines, startFullMoveNumber + 1 + i, white[i], black[i + 1]);
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

  function wireControls() {
    dom.btnBack.addEventListener("click", () => runCommand("back"));
    dom.btnFlip.addEventListener("click", () => runCommand("flip"));
    dom.btnCopyFen.addEventListener("click", copyFen);
    dom.btnSetFen.addEventListener("click", setFen);
    dom.btnCopyPgn.addEventListener("click", copyPgn);
    dom.btnRestart.addEventListener("click", () => runCommand("restart"));
    dom.btnEngine.addEventListener("click", () => {
      const current = state.data && state.data.engineMode === "strong" ? "strong" : "weak";
      runCommand(current === "strong" ? "engine weak" : "engine strong");
    });
    dom.promotionCancel.addEventListener("click", () => {
      state.selectedSquare = "";
      renderBoard();
      closePromotionDialog("");
    });
    dom.promotionDialog.addEventListener("click", (event) => {
      if (event.target === dom.promotionDialog) {
        state.selectedSquare = "";
        renderBoard();
        closePromotionDialog("");
      }
    });
    window.addEventListener("keydown", (event) => {
      if (event.key === "Escape" && state.promotionResolve) {
        state.selectedSquare = "";
        renderBoard();
        closePromotionDialog("");
      }
    });
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

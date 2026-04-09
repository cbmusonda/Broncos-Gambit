package version2;

import javax.swing.*;
import java.awt.*;
import java.awt.event.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.List;
import java.util.concurrent.*;

public class UciBoardArena2 extends JFrame {

    // ---------- UI ----------
    private enum SideType {ENGINE, MANUAL}

    private final JComboBox<SideType> whiteType = new JComboBox<>(SideType.values());
    private final JComboBox<SideType> blackType = new JComboBox<>(SideType.values());

    private final JTextField engineWhiteCmd = new JTextField("./../../Samples/team_c/run.sh");
    private final JTextField engineBlackCmd = new JTextField("./../../Samples/team_java/run.sh");

    private final JButton browseWhite = new JButton("Browse…");
    private final JButton browseBlack = new JButton("Browse…");

    private final JTextField thinkTimeMsField = new JTextField("300"); // per engine move
    private final JTextField delayMsField = new JTextField("500");     // between moves

    private final JLabel status = new JLabel("Ready.");
    private final JTextArea log = new JTextArea(14, 70);

    private final BoardPanel boardPanel = new BoardPanel();

    private JButton startBtn;
    private JButton stopBtn;

    // ---------- State ----------
    private volatile boolean running = false;

    private UciEngine whiteEngine;
    private UciEngine blackEngine;

    private ChessPosition pos = new ChessPosition();         // startpos
    private final List<String> movesUci = new ArrayList<>(); // from startpos
    private boolean whiteToMove = true;

    // Manual moves are delivered to the game loop via queue
    private final BlockingQueue<String> manualMoveQueue = new LinkedBlockingQueue<>();

    private final ExecutorService bg = Executors.newSingleThreadExecutor(r -> {
        Thread t = new Thread(r, "game-loop");
        t.setDaemon(true);
        return t;
    });

    public static void main(String[] args) {
        SwingUtilities.invokeLater(() -> new UciBoardArena2().setVisible(true));
    }

    public UciBoardArena2() {
        super("UCI Board Arena (Manual or Engine vs Manual or Engine)");
        setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);

        log.setEditable(false);
        log.setFont(new Font(Font.MONOSPACED, Font.PLAIN, 12));

        JPanel config = new JPanel(new GridBagLayout());
        GridBagConstraints c = new GridBagConstraints();
        c.insets = new Insets(4, 4, 4, 4);
        c.fill = GridBagConstraints.HORIZONTAL;

        int row = 0;
        addRow(config, c, row++, "White:", whiteType, "Engine command:", engineWhiteCmd, browseWhite);
        addRow(config, c, row++, "Black:", blackType, "Engine command:", engineBlackCmd, browseBlack);

        addRowSimple(config, c, row++, "Think time per engine move (ms):", thinkTimeMsField);
        addRowSimple(config, c, row++, "Delay between moves (ms):", delayMsField);

        startBtn = new JButton("Start");
        stopBtn = new JButton("Stop");
        stopBtn.setEnabled(false);

        startBtn.addActionListener(e -> startGame());
        stopBtn.addActionListener(e -> stopGame());

        browseWhite.addActionListener(e -> chooseEngine(engineWhiteCmd));
        browseBlack.addActionListener(e -> chooseEngine(engineBlackCmd));

        JPanel buttons = new JPanel(new FlowLayout(FlowLayout.LEFT));
        buttons.add(startBtn);
        buttons.add(stopBtn);

        JPanel left = new JPanel(new BorderLayout());
        left.add(config, BorderLayout.NORTH);
        left.add(buttons, BorderLayout.CENTER);
        left.add(new JScrollPane(log), BorderLayout.SOUTH);

        JPanel main = new JPanel(new BorderLayout());
        main.add(left, BorderLayout.WEST);
        main.add(boardPanel, BorderLayout.CENTER);
        main.add(status, BorderLayout.SOUTH);

        setContentPane(main);
        pack();
        setLocationRelativeTo(null);

        // Manual move input from the board
        boardPanel.setManualMoveListener(moveUci -> {
            if (!running) return;
            manualMoveQueue.offer(moveUci);
        });

        refreshEnablement();
        whiteType.addActionListener(e -> refreshEnablement());
        blackType.addActionListener(e -> refreshEnablement());

        boardPanel.setPosition(pos);
    }

    private void chooseEngine(JTextField target) {
        JFileChooser fc = new JFileChooser();
        fc.setFileSelectionMode(JFileChooser.FILES_ONLY);
        int res = fc.showOpenDialog(this);
        if (res == JFileChooser.APPROVE_OPTION) {
            File f = fc.getSelectedFile();
            // Use absolute path; user can add args manually if needed
            target.setText(f.getAbsolutePath());
        }
    }

    private void refreshEnablement() {
        boolean wEngine = whiteType.getSelectedItem() == SideType.ENGINE;
        boolean bEngine = blackType.getSelectedItem() == SideType.ENGINE;

        engineWhiteCmd.setEnabled(wEngine);
        browseWhite.setEnabled(wEngine);

        engineBlackCmd.setEnabled(bEngine);
        browseBlack.setEnabled(bEngine);
    }

    private void addRow(JPanel p, GridBagConstraints c, int row,
                        String sideLabel, JComponent sideChooser,
                        String cmdLabel, JComponent cmdField, JComponent browseBtn) {

        c.gridy = row;

        c.gridx = 0;
        c.weightx = 0;
        p.add(new JLabel(sideLabel), c);

        c.gridx = 1;
        c.weightx = 0.4;
        p.add(sideChooser, c);

        c.gridx = 2;
        c.weightx = 0;
        p.add(new JLabel(cmdLabel), c);

        c.gridx = 3;
        c.weightx = 1.0;
        p.add(cmdField, c);

        c.gridx = 4;
        c.weightx = 0;
        p.add(browseBtn, c);
    }

    private void addRowSimple(JPanel p, GridBagConstraints c, int row, String label, JComponent field) {
        c.gridy = row;
        c.gridx = 0;
        c.weightx = 0;
        p.add(new JLabel(label), c);
        c.gridx = 1;
        c.weightx = 1;
        c.gridwidth = 4;
        p.add(field, c);
        c.gridwidth = 1;
    }

    private void startGame() {
        if (running) return;
        running = true;

        startBtn.setEnabled(false);
        stopBtn.setEnabled(true);

        // reset game
        pos = new ChessPosition();
        movesUci.clear();
        whiteToMove = true;
        manualMoveQueue.clear();

        boardPanel.setPosition(pos);
        boardPanel.setTurn(whiteToMove);
        boardPanel.setManualEnabled(isManualToMove());
        boardPanel.clearSelection();
        boardPanel.repaint();

        log.setText("");
        setStatus("Game starting…");

        bg.submit(() -> {
            try {
                int thinkMs = parseIntOr(thinkTimeMsField.getText(), 300);
                int delayMs = parseIntOr(delayMsField.getText(), 500);

                // Start engines if needed
                if (whiteType.getSelectedItem() == SideType.ENGINE) {
                    whiteEngine = new UciEngine(engineWhiteCmd.getText().trim(), this::appendLog);
                    whiteEngine.startAndHandshake();
                    whiteEngine.send("ucinewgame");
                    whiteEngine.send("isready");
                    whiteEngine.waitFor("readyok", 3000);
                }
                if (blackType.getSelectedItem() == SideType.ENGINE) {
                    blackEngine = new UciEngine(engineBlackCmd.getText().trim(), this::appendLog);
                    blackEngine.startAndHandshake();
                    blackEngine.send("ucinewgame");
                    blackEngine.send("isready");
                    blackEngine.waitFor("readyok", 3000);
                }

                appendLog("=== Game started ===");
                updateUIForTurn();

                int ply = 0;
                while (running) {
                    SideType sideType = whiteToMove
                            ? (SideType) whiteType.getSelectedItem()
                            : (SideType) blackType.getSelectedItem();

                    String sideName = whiteToMove ? "White" : "Black";

                    String move;
                    if (sideType == SideType.MANUAL) {
                        setStatus(sideName + " to move (Manual). Click from-square then to-square.");
//                        move = waitForManualMove(60_000); // 60s to move (adjust if you want)
//                        if (move == null) {
//                            declareWinner(otherSideName(), sideName + " did not move in time (manual timeout).");
//                            break;
//                        }
//                        appendLog("[" + sideName + " manual] " + move);
                        while (true) {
                            move = waitForManualMove(60_000); // or whatever timeout you like
                            if (move == null) {
                                declareWinner(otherSideName(), sideName + " did not move in time (manual timeout).");
                                return; // end the game loop
                            }

                            // Validate WITHOUT changing the game state
                            MoveResult test = pos.validateOnlyUciMove(move, whiteToMove);
                            if (!test.ok) {
                                String msg = "Illegal move: " + move + "\nReason: " + test.reason + "\nTry again.";
                                appendLog("[" + sideName + " manual] rejected " + move + " (" + test.reason + ")");
                                SwingUtilities.invokeLater(() ->
                                        JOptionPane.showMessageDialog(this, msg, "Illegal Move", JOptionPane.WARNING_MESSAGE)
                                );

                                // Let them try again (do not stop game, do not forfeit)
                                continue;
                            }

                            // Accept the move
                            appendLog("[" + sideName + " manual] " + move);
                            break;
                        }

                    } else {
                        UciEngine eng = whiteToMove ? whiteEngine : blackEngine;
                        if (eng == null) {
                            declareWinner(otherSideName(), sideName + " engine not configured.");
                            break;
                        }

                        setStatus(sideName + " thinking… (engine movetime " + thinkMs + " ms)");
                        sendCurrentPositionToEngine(eng);
                        eng.send("go movetime " + thinkMs);

                        move = eng.waitForBestMove(thinkMs + 400); // buffer
                        if (move == null) {
                            declareWinner(otherSideName(), sideName + " engine timed out / no bestmove.");
                            break;
                        }
                        appendLog("[" + sideName + "] bestmove " + move);
                    }

                    // Validate + apply move. If illegal -> opponent wins.
                    MoveResult res = pos.validateAndApplyUciMove(move, whiteToMove);
                    if (!res.ok) {
                        declareWinner(otherSideName(), sideName + " played illegal move: " + move + " (" + res.reason + ")");
                        break;
                    }

                    movesUci.add(res.normalizedMoveUci);
                    whiteToMove = !whiteToMove;
                    ply++;

                    SwingUtilities.invokeLater(() -> {
                        boardPanel.setPosition(pos);
                        boardPanel.setTurn(whiteToMove);
                        boardPanel.clearSelection();
                        boardPanel.setManualEnabled(isManualToMove());
                        boardPanel.repaint();
                    });

                    updateUIForTurn();

                    sleep(delayMs);

                    if (ply >= 400) {
                        appendLog("Reached 400 plies, stopping.");
                        setStatus("Stopped (ply limit).");
                        break;
                    }
                }

            } catch (Exception ex) {
                appendLog("Fatal error: " + ex);
            } finally {
                cleanupEngines();
                SwingUtilities.invokeLater(() -> {
                    running = false;
                    startBtn.setEnabled(true);
                    stopBtn.setEnabled(false);
                    boardPanel.setManualEnabled(false);
                    boardPanel.clearSelection();
                    boardPanel.repaint();
                });
                appendLog("=== Stopped ===");
            }
        });
    }

    private void stopGame() {
        running = false;
        setStatus("Stopping…");
        stopBtn.setEnabled(false);
    }

    private void updateUIForTurn() {
        SwingUtilities.invokeLater(() -> {
            boardPanel.setManualEnabled(isManualToMove());
            boardPanel.setTurn(whiteToMove);
        });
    }

    private boolean isManualToMove() {
        SideType sideType = whiteToMove
                ? (SideType) whiteType.getSelectedItem()
                : (SideType) blackType.getSelectedItem();
        return sideType == SideType.MANUAL;
    }

    private String waitForManualMove(long timeoutMs) {
        try {
            return manualMoveQueue.poll(timeoutMs, TimeUnit.MILLISECONDS);
        } catch (InterruptedException e) {
            return null;
        }
    }

    private void sendCurrentPositionToEngine(UciEngine eng) throws IOException {
        StringBuilder sb = new StringBuilder("position startpos");
        if (!movesUci.isEmpty()) {
            sb.append(" moves");
            for (String m : movesUci) sb.append(" ").append(m);
        }
        eng.send(sb.toString());
    }

    private void declareWinner(String winner, String reason) {
        appendLog("=== RESULT: " + winner + " wins. Reason: " + reason + " ===");
        setStatus("Winner: " + winner + ". " + reason);
        running = false;
    }

    private String otherSideName() {
        return whiteToMove ? "Black" : "White";
    }

    private void cleanupEngines() {
        try {
            if (whiteEngine != null) whiteEngine.close();
        } catch (Exception ignored) {
        }
        try {
            if (blackEngine != null) blackEngine.close();
        } catch (Exception ignored) {
        }
        whiteEngine = null;
        blackEngine = null;
    }

    private void appendLog(String s) {
        SwingUtilities.invokeLater(() -> {
            log.append(s + "\n");
            log.setCaretPosition(log.getDocument().getLength());
        });
    }

    private void setStatus(String s) {
        SwingUtilities.invokeLater(() -> status.setText(s));
    }

    private static int parseIntOr(String s, int def) {
        try {
            return Integer.parseInt(s.trim());
        } catch (Exception e) {
            return def;
        }
    }

    private static void sleep(int ms) {
        try {
            Thread.sleep(ms);
        } catch (InterruptedException ignored) {
        }
    }

    // ---------------- UCI Engine Wrapper ----------------
    static class UciEngine implements Closeable {
        private final String commandLine;
        private final Consumer logger;

        private Process proc;
        private BufferedWriter stdin;
        private BufferedReader stdout;

        private final BlockingQueue<String> outLines = new LinkedBlockingQueue<>();
        private Thread readerThread;

        interface Consumer {
            void log(String s);
        }

        public UciEngine(String commandLine, Consumer logger) {
            this.commandLine = commandLine;
            this.logger = logger;
        }

        void startAndHandshake() throws IOException, TimeoutException {
            ProcessBuilder pb = new ProcessBuilder("cmd.exe", "/c", commandLine);
            pb.directory(new File(commandLine).getParentFile());
            pb.redirectErrorStream(true);
            proc = pb.start();

            stdin = new BufferedWriter(new OutputStreamWriter(proc.getOutputStream(), StandardCharsets.UTF_8));
            stdout = new BufferedReader(new InputStreamReader(proc.getInputStream(), StandardCharsets.UTF_8));

            readerThread = new Thread(this::readLoop, "uci-reader");
            readerThread.setDaemon(true);
            readerThread.start();

            send("uci");
            waitFor("uciok", 3000);
            send("isready");
            waitFor("readyok", 3000);

            logger.log("[engine] started: " + commandLine);
        }

        private void readLoop() {
            try {
                String line;
                while ((line = stdout.readLine()) != null) {
                    outLines.offer(line);
                }
            } catch (IOException ignored) {
            }
        }

        void send(String cmd) throws IOException {
            if (proc == null) throw new IOException("Engine not started");
            stdin.write(cmd);
            stdin.write("\n");
            stdin.flush();
        }

        void waitFor(String token, long timeoutMs) throws TimeoutException {
            long deadline = System.currentTimeMillis() + timeoutMs;
            while (System.currentTimeMillis() < deadline) {
                String line = pollLine(deadline - System.currentTimeMillis());
                if (line == null) break;
                if (line.contains(token)) return;
            }
            throw new TimeoutException("Timeout waiting for: " + token);
        }

        String waitForBestMove(long timeoutMs) {
            long deadline = System.currentTimeMillis() + timeoutMs;
            while (System.currentTimeMillis() < deadline) {
                String line = pollLine(deadline - System.currentTimeMillis());
                if (line == null) break;
                if (line.startsWith("bestmove ")) {
                    String[] parts = line.split("\\s+");
                    return parts.length >= 2 ? parts[1].trim() : null;
                }
            }
            return null;
        }

        private String pollLine(long timeoutMs) {
            try {
                return outLines.poll(Math.max(1, timeoutMs), TimeUnit.MILLISECONDS);
            } catch (InterruptedException e) {
                return null;
            }
        }

        @Override
        public void close() {
            try {
                if (proc != null) {
                    try {
                        send("quit");
                    } catch (Exception ignored) {
                    }
                    proc.destroy();
                }
            } finally {
                proc = null;
            }
        }
    }

    // ---------------- Minimal chess rules: validate & apply ----------------
    static class MoveResult {
        final boolean ok;
        final String reason;
        final String normalizedMoveUci;

        MoveResult(boolean ok, String reason, String norm) {
            this.ok = ok;
            this.reason = reason;
            this.normalizedMoveUci = norm;
        }

        static MoveResult ok(String norm) {
            return new MoveResult(true, "", norm);
        }

        static MoveResult bad(String reason) {
            return new MoveResult(false, reason, "");
        }
    }

    static class ChessPosition {
        // board[r][c], r=0 is rank 8, c=0 is file a
        private final char[][] b = new char[8][8];

        ChessPosition() {
            loadStartPos();
        }

        void loadStartPos() {
            String[] ranks = {
                    "rnbqkbnr",
                    "pppppppp",
                    "........",
                    "........",
                    "........",
                    "........",
                    "PPPPPPPP",
                    "RNBQKBNR"
            };
            for (int r = 0; r < 8; r++)
                for (int c = 0; c < 8; c++)
                    b[r][c] = ranks[r].charAt(c);
        }

        char pieceAt(int r, int c) {
            return b[r][c];
        }

        MoveResult validateAndApplyUciMove(String m, boolean whiteToMove) {
            if (m == null) return MoveResult.bad("null move");
            m = m.trim();
            if (m.length() < 4) return MoveResult.bad("bad UCI move format");

            int fromC = m.charAt(0) - 'a';
            int fromR = 8 - (m.charAt(1) - '0');
            int toC = m.charAt(2) - 'a';
            int toR = 8 - (m.charAt(3) - '0');

            if (!inBounds(fromR, fromC) || !inBounds(toR, toC)) return MoveResult.bad("out of bounds");

            char p = b[fromR][fromC];
            if (p == '.') return MoveResult.bad("No piece at from-square");
            if (whiteToMove && !Character.isUpperCase(p)) return MoveResult.bad("not white piece");
            if (!whiteToMove && !Character.isLowerCase(p)) return MoveResult.bad("not black piece");

            char target = b[toR][toC];
            if (target != '.' && sameColor(p, target)) return MoveResult.bad("cannot capture own piece");

            // Castling (basic)
            if ((p == 'K' || p == 'k') && Math.abs(toC - fromC) == 2 && fromR == toR) {
                // We only check squares between are empty; we do not check check legality here.
                if (toC == 6) { // king side
                    if (b[toR][5] != '.' || b[toR][6] != '.') return MoveResult.bad("castling path blocked");
                    char rook = b[toR][7];
                    if (rook != (whiteToMove ? 'R' : 'r')) return MoveResult.bad("rook missing for castling");
                    // apply
                    b[toR][6] = p;
                    b[fromR][fromC] = '.';
                    b[toR][5] = rook;
                    b[toR][7] = '.';
                    return MoveResult.ok(m.substring(0, 4));
                } else if (toC == 2) { // queen side
                    if (b[toR][1] != '.' || b[toR][2] != '.' || b[toR][3] != '.')
                        return MoveResult.bad("castling path blocked");
                    char rook = b[toR][0];
                    if (rook != (whiteToMove ? 'R' : 'r')) return MoveResult.bad("rook missing for castling");
                    b[toR][2] = p;
                    b[fromR][fromC] = '.';
                    b[toR][3] = rook;
                    b[toR][0] = '.';
                    return MoveResult.ok(m.substring(0, 4));
                } else {
                    return MoveResult.bad("bad castling target");
                }
            }

            // Validate piece movement (pseudo-legal; no check detection; no en passant)
            if (!isPseudoLegalMove(p, fromR, fromC, toR, toC, whiteToMove)) {
                return MoveResult.bad("illegal move for piece");
            }

            // Promotion
            String norm = m.substring(0, 4);
            if (isPawn(p) && (toR == 0 || toR == 7)) {
                char promo = (m.length() >= 5) ? m.charAt(4) : 'q';
                if ("qrbnQRBN".indexOf(promo) < 0) promo = 'q';
                promo = whiteToMove ? Character.toUpperCase(promo) : Character.toLowerCase(promo);
                b[toR][toC] = promo;
                b[fromR][fromC] = '.';
                norm = norm + Character.toLowerCase(promo); // UCI uses lowercase letter for promotion piece
                return MoveResult.ok(norm);
            }

            // Apply normal move
            b[toR][toC] = p;
            b[fromR][fromC] = '.';
            return MoveResult.ok(norm);
        }

        private boolean isPseudoLegalMove(char p, int fr, int fc, int tr, int tc, boolean white) {
            int dr = tr - fr;
            int dc = tc - fc;

            if (p == 'N' || p == 'n') {
                int adr = Math.abs(dr), adc = Math.abs(dc);
                return (adr == 2 && adc == 1) || (adr == 1 && adc == 2);
            }
            if (p == 'B' || p == 'b') {
                if (Math.abs(dr) != Math.abs(dc)) return false;
                return clearLine(fr, fc, tr, tc);
            }
            if (p == 'R' || p == 'r') {
                if (dr != 0 && dc != 0) return false;
                return clearLine(fr, fc, tr, tc);
            }
            if (p == 'Q' || p == 'q') {
                if (dr == 0 || dc == 0 || Math.abs(dr) == Math.abs(dc)) {
                    return clearLine(fr, fc, tr, tc);
                }
                return false;
            }
            if (p == 'K' || p == 'k') {
                return Math.max(Math.abs(dr), Math.abs(dc)) == 1;
            }
            if (p == 'P' || p == 'p') {
                int dir = white ? -1 : 1;
                // forward
                if (dc == 0) {
                    if (dr == dir && b[tr][tc] == '.') return true;
                    // two squares from start rank
                    int startR = white ? 6 : 1;
                    if (fr == startR && dr == 2 * dir && b[fr + dir][fc] == '.' && b[tr][tc] == '.') return true;
                    return false;
                } else if (Math.abs(dc) == 1 && dr == dir) {
                    // capture
                    return b[tr][tc] != '.' && !sameColor(p, b[tr][tc]);
                }
                return false;
            }
            return false;
        }

        private boolean clearLine(int fr, int fc, int tr, int tc) {
            int dr = Integer.compare(tr, fr);
            int dc = Integer.compare(tc, fc);
            int r = fr + dr, c = fc + dc;
            while (r != tr || c != tc) {
                if (b[r][c] != '.') return false;
                r += dr;
                c += dc;
            }
            return true;
        }

        ChessPosition copy() {
            ChessPosition cp = new ChessPosition();
            cp.copyFrom(this);
            return cp;
        }

        private void copyFrom(ChessPosition other) {
            for (int r = 0; r < 8; r++) {
                System.arraycopy(other.b[r], 0, this.b[r], 0, 8);
            }
        }

        /**
         * Validate move without mutating this position.
         */
        MoveResult validateOnlyUciMove(String m, boolean whiteToMove) {
            ChessPosition tmp = this.copy();
            return tmp.validateAndApplyUciMove(m, whiteToMove);
        }


        private boolean inBounds(int r, int c) {
            return r >= 0 && r < 8 && c >= 0 && c < 8;
        }

        private boolean sameColor(char a, char x) {
            return Character.isUpperCase(a) == Character.isUpperCase(x);
        }

        private boolean isPawn(char p) {
            return p == 'P' || p == 'p';
        }
    }

    // ---------------- Board UI (Unicode pieces + manual clicks) ----------------
    static class BoardPanel extends JPanel {
        private ChessPosition pos;
        private boolean whiteToMove = true;
        private boolean manualEnabled = false;

        private Point selected = null; // (c,r)
        private ManualMoveListener manualMoveListener;

        interface ManualMoveListener {
            void onMove(String moveUci);
        }

        BoardPanel() {
            setPreferredSize(new Dimension(520, 520));
            addMouseListener(new MouseAdapter() {
                @Override
                public void mousePressed(MouseEvent e) {
                    if (!manualEnabled || pos == null) return;
                    handleClick(e.getX(), e.getY());
                }
            });
        }

        void setPosition(ChessPosition p) {
            this.pos = p;
        }

        void setTurn(boolean whiteToMove) {
            this.whiteToMove = whiteToMove;
        }

        void setManualEnabled(boolean enabled) {
            this.manualEnabled = enabled;
            repaint();
        }

        void setManualMoveListener(ManualMoveListener l) {
            this.manualMoveListener = l;
        }

        void clearSelection() {
            selected = null;
        }

        private void handleClick(int x, int y) {
            int size = Math.min(getWidth(), getHeight());
            int sq = size / 8;
            int c = x / sq;
            int r = y / sq;
            if (c < 0 || c > 7 || r < 0 || r > 7) return;

            if (selected == null) {
                char p = pos.pieceAt(r, c);
                if (p == '.') return;
                if (whiteToMove && !Character.isUpperCase(p)) return;
                if (!whiteToMove && !Character.isLowerCase(p)) return;
                selected = new Point(c, r);
                repaint();
                return;
            }

            // second click => attempt move
            int fromC = selected.x, fromR = selected.y;
            int toC = c, toR = r;

            String uci = toUci(fromR, fromC, toR, toC);

            // Promotion prompt if pawn reaching last rank (we can't fully know legality here, but it's fine)
            char piece = pos.pieceAt(fromR, fromC);
            boolean isPawn = (piece == 'P' || piece == 'p');
            if (isPawn && (toR == 0 || toR == 7)) {
                String[] opts = {"q", "r", "b", "n"};
                String choice = (String) JOptionPane.showInputDialog(
                        this,
                        "Promotion piece:",
                        "Promotion",
                        JOptionPane.PLAIN_MESSAGE,
                        null,
                        opts,
                        "q"
                );
                if (choice != null && !choice.isBlank()) uci = uci + choice.trim().toLowerCase();
                else uci = uci + "q";
            }

            selected = null;
            repaint();

            if (manualMoveListener != null) manualMoveListener.onMove(uci);
        }

        private String toUci(int fromR, int fromC, int toR, int toC) {
            char fFile = (char) ('a' + fromC);
            char fRank = (char) ('0' + (8 - fromR));
            char tFile = (char) ('a' + toC);
            char tRank = (char) ('0' + (8 - toR));
            return "" + fFile + fRank + tFile + tRank;
        }

        @Override
        protected void paintComponent(Graphics g) {
            super.paintComponent(g);
            if (pos == null) return;

            int size = Math.min(getWidth(), getHeight());
            int sq = size / 8;

            // squares
            for (int r = 0; r < 8; r++) {
                for (int c = 0; c < 8; c++) {
                    boolean light = ((r + c) % 2 == 0);
                    g.setColor(light ? new Color(240, 217, 181) : new Color(181, 136, 99));
                    g.fillRect(c * sq, r * sq, sq, sq);
                }
            }

            // selection highlight
            if (selected != null) {
                g.setColor(new Color(80, 180, 255, 120));
                g.fillRect(selected.x * sq, selected.y * sq, sq, sq);
            }

            // pieces (Unicode chess symbols)
            Font pieceFont = findPieceFont((int) (sq * 0.75));
            g.setFont(pieceFont);

            for (int r = 0; r < 8; r++) {
                for (int c = 0; c < 8; c++) {
                    char p = pos.pieceAt(r, c);
                    if (p == '.') continue;
                    String sym = pieceSymbol(p);
                    if (sym == null) continue;

                    // draw centered
                    FontMetrics fm = g.getFontMetrics();
                    int tx = c * sq + (sq - fm.stringWidth(sym)) / 2;
                    int ty = r * sq + (sq + fm.getAscent() - fm.getDescent()) / 2;

                    // black outline-ish (simple)
                    g.setColor(Color.BLACK);
                    g.drawString(sym, tx, ty);
                }
            }

            // manual status overlay
            if (manualEnabled) {
                g.setColor(new Color(0, 0, 0, 140));
                g.fillRoundRect(10, 10, 230, 28, 12, 12);
                g.setColor(Color.WHITE);
                g.setFont(new Font(Font.SANS_SERIF, Font.PLAIN, 14));
                g.drawString("Manual input enabled", 20, 30);
            }
        }

        private Font findPieceFont(int size) {
            // Many macOS setups support Unicode chess glyphs in default fonts,
            // but try a few known fonts for better odds.
            String[] candidates = {"Apple Symbols", "Segoe UI Symbol", "Noto Sans Symbols2", "Symbola", Font.SANS_SERIF};
            for (String name : candidates) {
                Font f = new Font(name, Font.PLAIN, size);
                if (f.canDisplay('\u2654')) return f; // white king
            }
            return new Font(Font.SANS_SERIF, Font.PLAIN, size);
        }

        private String pieceSymbol(char p) {
            return switch (p) {
                case 'K' -> "\u2654"; // ♔
                case 'Q' -> "\u2655"; // ♕
                case 'R' -> "\u2656"; // ♖
                case 'B' -> "\u2657"; // ♗
                case 'N' -> "\u2658"; // ♘
                case 'P' -> "\u2659"; // ♙
                case 'k' -> "\u265A"; // ♚
                case 'q' -> "\u265B"; // ♛
                case 'r' -> "\u265C"; // ♜
                case 'b' -> "\u265D"; // ♝
                case 'n' -> "\u265E"; // ♞
                case 'p' -> "\u265F"; // ♟
                default -> null;
            };
        }
    }
}


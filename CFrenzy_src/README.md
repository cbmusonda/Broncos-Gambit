# CFrenzy Chess Engine

## Team Name and Members
**Team Name:** CFrenzy

**Members:**
- Heba Hafez
- Cirena Arabit
- Christian Ruelas
- Changwe Musonda
- Mariam Nadeem
- Jessalin Jiangkhov

## Language
C (C99)

## How to Build and Run with Arena

### Building
1. Run `build.bat` — this compiles the source and produces `engine.exe`
2. Confirm `engine.exe` appears in the folder — if it does, the build succeeded

### Running with Arena
1. Open the Arena Chess GUI
2. Go to `Engines` option
3. Browse to and select `run.bat` (not `engine.exe` directly)
4. Engine is ready to start. Click the start button to begin.

## Supported Chess Rules
- All standard piece movements (King, Queen, Rook, Bishop, Knight, Pawn)
- Castling (king may not castle through or into check)
- Pawn promotion (queen, rook, bishop, knight)
- Captures
- Legal move validation (moves leaving king in check are rejected)
- UCI commands: `uci`, `isready`, `ucinewgame`, `position`, `go movetime`, `quit`

## Rule Modifications
- **En passant is not supported** (disabled per project specification)

## Known Limitations
- No castling rights tracking — if a king or rook returns to its starting
  square after moving, castling may be incorrectly offered
- No fifty-move rule or insufficient material draw detection
- Search depth is fixed at 5 with time-based early exit; may be slower
  in very complex positions

```mermaid
sequenceDiagram
    autonumber
    actor Host as Host / GUI (Arena)
    participant Engine as Engine(Main loop)
    participant Parser as UCI Parser
    participant Position as Position (Board/FEN) 
    participant MoveGen as MoveGenerator (pseudoLegalMoves etc.)
    participant Rule as RuleChecker (isSquareAttacked / inCheck)
    participant MoveObj as Move (Move.fromUci / toUCI)
   
   Host ->> Engine: "uci"
   activate Engine
   Engine ->> Engine: parse "uci"
   Engine -->> Host: "id name <engine>"
   Engine -->> Host: "id author <author>"
   Engine -->> Host: "uciok"
   deactivate Engine

    Host -->> Engine: "isready"
    activate Engine
    Engine ->> Engine: parse "isready"
    Engine -->> Host: "readyok"
    deactivate Engine

    Host -->> Engine: "ucinewgame"
    activate Engine
    Engine ->> Position: Position.startPos()
    Engine -->> Host: (ack no response required)
    deactivate Engine

    Host -->> Engine: "position startpos move e2e4 e7e5 ..."
    activate Engine
    Engine ->> Parser: parse "position ..." (detect startpos (or fen))
    Parser ->> Position: create Position (start Pos (or fromFEN))
    loop for each move token
        Parser ->> MoveOBj: Move.fromUci("e2e4")
        MoveObj -->> Parser: Move object
        Parser ->> Position: Position = Position.makeMove(Move)
    end
    Engine -->> Host: (no response required)
    deactivate Engine
   
    Host ->> Engine: "go movetime 10000"
    activate Engine
    Engine ->> Parser: parse "go" options (movetime/wtime/etc.)
    Engine ->> MoveGen: legalMoves = Position.legalMoves()
    activate MoveGen

    MoveGen ->> Position: getBoardState()
    Position -->> MoveGen: pieces, sideToMove, castlingRights, enPassantSquare
    MoveGen ->> MoveGen: generatePseudoLegalMoves(boardState)
    loop for each pseudoLegal move
        MoveGen ->> MoveObj: Move.fromUCI(candidateUCI)
        MoveObj -->> MoveGen: Move object
        MoveGen ->> Position: tempPosition = position.makeMove(move)
        Position -->> MoveGen: tempPosition
        MoveGen ->> Rule: isInCheck(tempPosition, sideToMove)
        activate Rule
        Rule ->> Position: getKingSquare(tempPosition, sideToMove)
        Position -->> Rule: kingSquare
        Rule ->> Rule: isSquareAttacked(kingSquare, opponent, tempPosition)
        Rule -->> MoveGen: true = illegal / false = legal
        deactivate Rule
        alt move is legal (king NOT in check)
            MoveGen ->> MoveGen: add move to legalMoves list
        else move is illegal (king left in check)
            MoveGen ->> MoveGen: discard move
        end
    end

    MoveGen -->> Engine: returns legalMoves list
    deactivate MoveGen

    Engine ->> Engine: choose first move (legalMoves.get(0))
    Engine ->> MoveObj: selectedMove.toUci()
    Engine -->> Host: "bestmove e2e4"
    deactivate Engine

    Host ->> Engine: "quit"
    activate Engine
    Engine ->> Engine: cleanup and exit
    Engine -->> Host: (process ends)
    deactivate Engine
```  
``` 

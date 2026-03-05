```mermaid
sequenceDiagram
    autonumber
    actor Host as Host / GUI (Arena)
    participant Engine as Engine(Main loop)
    participant Parser as UCI Parser
    participant Position as Position (Board/FEN) 
    participant MoveGen as MoveGenerator (pseudoLegalMoves etc.)
    participant Rule as RuleChecker (isSquareAttacked / inCheck)
    participant MoveObj as Move (Move.fromUco / toUCI)

   %%Note over Host,Engine: Engine runs a UCI stdin/stdout loop

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

``` 

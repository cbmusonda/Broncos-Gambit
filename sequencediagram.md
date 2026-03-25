Host ->> Engine: "go movetime 10000"
activate Engine
Engine ->> Parser: parse "go" pptions (movetime/wtime/etc.)   
Engine ->> MoveGen: legalMoves = Position.legalMoves()
activate MoveGen
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

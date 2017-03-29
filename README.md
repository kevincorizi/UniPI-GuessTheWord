# UniPI-GuessTheWord
P2P online game with centralized scheduling server to manage multiple pairs of users at the same time.

The game is developed in C using the C socket API.

The server is implemented with a single process with I/O multiplexing. It accepts TCP requests from players who want to start a game and keeps track of ongoing games.

The client interacts with the server to find and challenge players with TCP messages. The game is implemented as an exchange of UDP messages directly between the players, with no interaction with the server.

At the end of the game, the winner notifies the server of the victory so that it can inform the other player and free the resources dedicated to the current game.

Here is a brief description of the protocol:

![Protocol](/protocol.png)
                                                

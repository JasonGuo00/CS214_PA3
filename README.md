# PA3

Creators    :
Armen Karakashian   netID avk56
Jason Guo           netID jg1715

Design Note:

- When a player breaks protocol, they receive INVL messages rather than flat out disconnections.

Test Plan   :



First, we wanted to make sure that the core game worked when player conformed to the protocol properly.

- We had the server run on one of our ilabs as we connected with the ttt client on separate terminals.
- We both connected to the server, began the game with the PLAY message, and played the game making sure that there would be a winner.  This checks that the game detects wins and losses properly.
- We then ran another game, this time making sure to draw.  This checks that the game detects draws properly.
- We ran another game, this time with one of us using the RSGN message in the middle of the game while it is not their turn.  This checks that the game detects RSGN messages properly and that interruptions were working.
- We ran one more game, this time with one of us using the DRAW message in the middle of the game while it is not their turn.  This checks that the game detects DRAW messages properly and that interruptions were working.



Second, we now wanted to make sure that our server could handle all sorts of bad commands.  We ran a few more games breaking protocol all over the place.

- We ran a game and before we could start it with PLAY, one of us disconnected using ctrl-c.  This caused the connected client to also disconnect.  This ensures that if a player breaks connection before the game begins, then the game is tossed out and the other player kicked.
- We did the same thing as before but this time while the game was running.  Also ensures that player disconnections are handled.
- We ran the game, inputting anything other than a proper PLAY message while the server was waiting for players to signal readiness and choose usernames.  Expected outputs lined up with the actual outputs, which were a bunch of INVL responses.
- We ran the game with both players choosing the same name.  The response was an INVL message for the player who put it in last, which ensures that our server handles username checking.
- We ran the game with one player choosing a name greater than 254 characters, which caused an INVL message, ensuring that messages were limited to 254 characters, and therefore implicitly the same implies for usernames.
- We ran the game and attempted to make moves while it wasn't our turn.  The response was an INVL message, ensuring that the server handles tracking who's turn it is while still listening to both players.  We further attempted making a MOVE with incorrect roles (moving O while you're assigned X, for example).  This ensured that the server will only let the player move their own role.
- We ran the game with one player requesting a DRAW.  After that, we attempted to have the requester make moves and more requests, which received INVL messages as a response.  Next, we had the requested attempt to make commands that weren't DRAW A or DRAW R, to which we received INVL messages.  This insures that the requested must respond to the DRAW request with a proper answer.




Finally, we wanted to test that concurrency worked.

- We started one server, and then split an even amount of terminals (6 in testing) to connect to the server
- The 6 were paired up based on the order of connection, and the games were played through without errors.
- During the waiting phase where players used PLAY, we chose our player names in the first game and then attempted to use those names again in the other games, to which we received INVL messages, ensuring that the threads that the separate games run in properly check the shared list of usernames and disallow repeats.

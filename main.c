#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/soundcard.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <errno.h>
#include <inttypes.h>

#ifndef NULL
#define NULL ((void*)0)
#endif

/* grid dimension : 10x18 */
/**
 * \def WIDTH
 * \brief Total width of the board
 */
#define WIDTH 19

/**
 * \def MIN
 * \brief Minimum of two values
 */
#define MIN(a, b) ((a) < (b) ? (a) : (b))

/**
 * \def MAX
 * \brief Maximum of two values
 */
#define MAX(a, b) ((a) > (b) ? (a) : (b))

/**
 * \def HEIGHT
 * \brief Total height of the board including player's information column
 */
#define HEIGHT 19

/**
 * \def INITIAL_PERIOD
 * \brief Number of cycles it takes a piece to drop of 1
 */
#define INITIAL_PERIOD 50

/**
 * \def INTER_FRAME
 * \brief Approximate inter-frame duration
 */
#define INTER_FRAME 23000

/**
 * \def BUF_SIZE
 * \brief Size of the audio buffers
 */
#define BUF_SIZE 2048

/**
 * \def SFX
 * \brief Directory which contains sfx raw 8 bits unsigned audio sfx
 */
#define SFX "sound/sfx/"

/* Network modes */
/**
 * \def NET_NONE
 * \brief Single player mode
 */
#define NET_NONE 0

/**
 * \def NET_SERVER
 * \brief Two player mode, server
 */
#define NET_SERVER 1

/**
 * \def NET_CLIENT
 * \brief Two player mode, client
 */
#define NET_CLIENT 2

/**
 * \def NET_DEFAULT_PORT
 * \brief Default port used for two player mode TODO not used for now
 */
#define NET_DEFAULT_PORT 37280

/* Message codes are on the first (big) 3 bytes, value on the last five */
/**
 * \def MSG_CODE
 * \brief Extracts the code part of a network message
 */
#define MSG_CODE(msg) (0xE0 & (msg))

/**
 * \def MSG_VALUE
 * \brief Extracts the value part of a network message
 */
#define MSG_VALUE(msg) (0x1F & (msg))

/**
 * \def MSG_HEIGHT
 * \brief Network message indicating the current height the player is at
 */
#define MSG_HEIGHT 0x00

/**
 * \def MSG_LINES
 * \brief Network message to send lines to the other player
 */
#define MSG_LINES 0x20

/**
 * \def MSG_LOST
 * \brief Network message to indicate that the player has lost
 */
#define MSG_LOST 0x40

/**
 * \def MSG_QUIT
 * \brief Network message to indicate that the player has quit
 */
#define MSG_QUIT 0x60

/**
 * \def MSG_PAUSE
 * \brief Network message to indicate that the player has paused the game
 */
#define MSG_PAUSE 0x80

/**
 * \def MSG_BUILD
 * \brief Builds a message by packing together the message code an the
 * associated value
 */
#define MSG_BUILD(code, value) ((char)((code) | (value)))

/**
 * \var clear
 * \brief Console escape sequence for clearing the terminal
 */
static const char clear[] = {0x1b, 0x5b, 0x48, 0x1b, 0x5b, 0x4a, 0};

/**
 * \var civis
 * \brief Console escape sequence to make the cursor invisible
 */
static const char civis[] = {0x1b, 0x5b, 0x3f, 0x32, 0x35, 0x6c, 0};

/**
 * \var cnorm
 * \brief Console escape sequence to make the cursor visible
 */
static const char cnorm[] = {
	0x1b, 0x5b, 0x33, 0x34, 0x68, 0x1b, 0x5b, 0x3f, 0x32, 0x35, 0x68, 0
};

/**
 * \var sgr0
 * \brief Console escape sequence to put back the font fore and back ground
 * colors to normal
 */
static const char sgr0[] = {0x1b, 0x5b, 0x6d, 0x0f, 0};

/**
 * \var board
 * \brief Complete board containing the playing grid and the user informations
 */
static char board[19][20] = {
	"*          ********",
	"*          *score**",
	"*          ********",
	"*          *     0*",
	"*          ********",
	"*          *level**",
	"*          ********",
	"*          *     0*",
	"*          ********",
	"*          *lines**",
	"*          ********",
	"*          *     0*",
	"*          ********",
	"*          ***    *",
	"*          ***    *",
	"*          ***    *",
	"*          ***    *",
	"*          ********",
	"*******************",
};

/**
 * \enum end_status
 * \brief status when the game ends
 */
enum end_status {
	END_NONE,      /**< The game is still on going */
	END_WON,       /**< The player won the game, in mode 2 or b */
	END_LOST,      /**< The player has lost */
	END_QUIT,      /**< The player has requested to quit the game */
	END_PEER_LEFT, /**< The remote has quit in a 2 player game */
};

/**
 * \var game
 * \brief Main structure representing the game's current state
 */
struct {
	char mode;    /**< Game mode, 'a', 'b' or '2' (for two players) */
	int high;     /**< Height of the handicap */
	int lvl;      /**< Speed of pieces dropping */
	int lines;    /**< Number of lines achieved so far */
	int score;    /**< Current score of the player */
	int period;   /**< Number of loop passes between two automatic drops */
	int pause;    /**< If true, the game is suspended */
	int height;   /**< Current height of the game, for network mode only */
	int void_col; /**< Index of the void column for penalties */
	int loop;     /**< Non-zero while in main loop */
	int freeze;   /**< Number of frame when a down can't occur (key delay) */
	int music;    /**< Non zero if music is enabled */
	int dsp;      /**< File descriptor of sound card */
	int bgm;      /**< File descriptor of background music */
	int sfx;      /**< Sfx currently playing, -1 if none */
	int suspended;/**< Number of frame during when the game is suspended */
	/** status when the game is finished */
	enum end_status status;
} game = {
		.mode =     'a',
		.high =     0,
		.lvl =      0,
		.lines =    0,
		.score =    0,
		.period =   INITIAL_PERIOD,
		.pause =    0,
		.height =   0,
		.void_col = 0,
		.loop =     1,
		.freeze =   0,
		.music =    0,
		.dsp =      -1,
		.bgm =      -1,
		.sfx =      -1,
		.status =   END_NONE,
		.suspended = 0,
};

/**
 * \var net
 * \brief Network related informations. Not used if mode if NET_NONE (single
 * player)
 */
struct {
	int mode;   /**< One of NET_NONE, NET_SERVER and NET_CLIENT */
	char *addr; /**< TODO make the program accept any address Address of the server */
	int port;   /**< Port number of the connection */
	int sfd;    /**< Socket of the server, only used by the server */
	int fd;     /**< socket of the remote host */
	int pending_lines; /**< Number of lines to send to the remote */
} net = {
		.mode = NET_NONE,
		.addr = "localhost",
		.port = NET_DEFAULT_PORT,
		.sfd = -1,
		.fd = -1,
		.pending_lines = 0,
};

/**
 * \typedef image
 * \brief type of the image a o given rotation of a piece
 * TODO define it as 2 bytes (uint16_t if no better choice) and add a getter
 * for A0, it will give :0000 0000 1111 0000, hence 0x00F0
 */
typedef char image[4][4];

//#define PIXEL_LIT(im, x, y) (((1 << (x)) + 4 * (y)) & im)
#define PIXEL_LIT(im, x, y) (' ' != (*(im))[(y)][(x)])

// Retourne un pointeur sur l'image
#define GET_IMG(scl, piece, ori) ((*((scl)[(piece)]))[(ori)])

#define VALID_IMG(im) ((im) != NULL)

/**
 * \var A0
 * \brief line shaped piece
 */
static const image A0 = {
	{' ', ' ', ' ', ' '},
	{' ', ' ', ' ', ' '},
	{'L', 'L', 'L', 'L'},
	{' ', ' ', ' ', ' '},
};

static const image A1 = {
	{' ', 'L', ' ', ' '},
	{' ', 'L', ' ', ' '},
	{' ', 'L', ' ', ' '},
	{' ', 'L', ' ', ' '},
};

/**
 * \var B0
 * \brief Block shaped piece
 */
#undef B0 /* Baud rate constant from termios.h */
static const image B0 = {
	{' ', ' ', ' ', ' '},
	{' ', 'S', 'S', ' '},
	{' ', 'S', 'S', ' '},
	{' ', ' ', ' ', ' '},
};

/**
 * \var C0
 * \brief Tee shaped piece
 */
static const image C0 = {
	{' ', ' ', ' ', ' '},
	{'D', 'D', 'D', ' '},
	{' ', 'D', ' ', ' '},
	{' ', ' ', ' ', ' '},
};

static const image C1 = {
	{' ', 'D', ' ', ' '},
	{'D', 'D', ' ', ' '},
	{' ', 'D', ' ', ' '},
	{' ', ' ', ' ', ' '},
};

static const image C2 = {
	{' ', 'D', ' ', ' '},
	{'D', 'D', 'D', ' '},
	{' ', ' ', ' ', ' '},
	{' ', ' ', ' ', ' '},
};

static const image C3 = {
	{' ', 'D', ' ', ' '},
	{' ', 'D', 'D', ' '},
	{' ', 'D', ' ', ' '},
	{' ', ' ', ' ', ' '},
};

/**
 * \var D0
 * \brief S shaped piece
 */
static const image D0 = {
	{' ', ' ', ' ', ' '},
	{' ', 'R', 'R', ' '},
	{'R', 'R', ' ', ' '},
	{' ', ' ', ' ', ' '},
};

static const image D1 = {
	{'R', ' ', ' ', ' '},
	{'R', 'R', ' ', ' '},
	{' ', 'R', ' ', ' '},
	{' ', ' ', ' ', ' '},
};

/**
 * \var E0
 * \brief Z shaped piece
 */
static const image E0 = {
	{' ', ' ', ' ', ' '},
	{'M', 'M', ' ', ' '},
	{' ', 'M', 'M', ' '},
	{' ', ' ', ' ', ' '},
};

static const image E1 = {
	{' ', 'M', ' ', ' '},
	{'M', 'M', ' ', ' '},
	{'M', ' ', ' ', ' '},
	{' ', ' ', ' ', ' '},
};

/**
 * \var F0
 * \brief L shaped piece
 */
static const image F0 = {
	{' ', ' ', ' ', ' '},
	{'F', 'F', 'F', ' '},
	{'F', ' ', ' ', ' '},
	{' ', ' ', ' ', ' '},
};

static const image F1 = {
	{'F', 'F', ' ', ' '},
	{' ', 'F', ' ', ' '},
	{' ', 'F', ' ', ' '},
	{' ', ' ', ' ', ' '},
};

static const image F2 = {
	{' ', ' ', 'F', ' '},
	{'F', 'F', 'F', ' '},
	{' ', ' ', ' ', ' '},
	{' ', ' ', ' ', ' '},
};

static const image F3 = {
	{' ', 'F', ' ', ' '},
	{' ', 'F', ' ', ' '},
	{' ', 'F', 'F', ' '},
	{' ', ' ', ' ', ' '},
};

/**
 * \var G0
 * \brief J shaped piece
 */
static const image G0 = {
	{' ', ' ', ' ', ' '},
	{'S', 'S', 'S', ' '},
	{' ', ' ', 'S', ' '},
	{' ', ' ', ' ', ' '},
};

static const image G1 = {
	{' ', 'S', ' ', ' '},
	{' ', 'S', ' ', ' '},
	{'S', 'S', ' ', ' '},
	{' ', ' ', ' ', ' '},
};

static const image G2 = {
	{'S', ' ', ' ', ' '},
	{'S', 'S', 'S', ' '},
	{' ', ' ', ' ', ' '},
	{' ', ' ', ' ', ' '},
};

static const image G3 = {
	{' ', 'S', 'S', ' '},
	{' ', 'S', ' ', ' '},
	{' ', 'S', ' ', ' '},
	{' ', ' ', ' ', ' '},
};

typedef image const *sprite[5];

static const sprite A = {&A0,  &A1, NULL, NULL, NULL};
static const sprite B = {&B0, NULL, NULL, NULL, NULL};
static const sprite C = {&C0,  &C1,  &C2,  &C3, NULL};
static const sprite D = {&D0,  &D1, NULL, NULL, NULL};
static const sprite E = {&E0,  &E1, NULL, NULL, NULL};
static const sprite F = {&F0,  &F1,  &F2,  &F3, NULL};
static const sprite G = {&G0,  &G1,  &G2,  &G3, NULL};

sprite const *scale[7] = {&A, &B, &C, &D, &E, &F, &G};

struct {
	int piece;
	int next_piece;
	int ori;
	int next_ori;
	int x;
	int next_x;
	int y;
	int next_y;
	int hit;
} current = {
	.y =        0,
	.next_y =   0,
	.x =        3,
	.next_x =   3,
	.ori =      0,
	.next_ori = 0,
	.hit =      0,
};

/**
 * \def WRITE
 * \brief Writes a single char to standard output
 */
#define WRITE(c) do { \
	char _buf = (char)(c); \
	if (write(1, &_buf, 1)); \
} while (0)

/**
 * @brief Writes a 0 terminated set of char to standard output
 * @param s Set of char ending with a 0
 */
#define WRITES(s) do { \
	/* size_t _i = 0; */\
	unsigned _i = 0; \
	while (s[_i]) \
		_i++; \
	if (write(1, s, _i)); \
} while(0)

enum sfx {
        SFX_DROP,       /**<  */
        SFX_GRID_DROP,  /**<  */
        SFX_LINE,       /**<  */
        SFX_LOST,       /**<  */
        SFX_MOVE,       /**<  */
        SFX_PAUSE,      /**<  */
        SFX_ROTATION,   /**<  */
        SFX_TETRIS,     /**<  */
        SFX_WIN,        /**<  */

	SFX_NB,		/**<  */
};

static struct {
	const char *path;
	int fd;
} sfx_file[] = {
        {SFX"Drop.raw",         -1},
        {SFX"Grid_drop.raw",    -1},
        {SFX"Line.raw",         -1},
        {SFX"Lost.raw",         -1},
        {SFX"Move.raw",         -1},
        {SFX"Pause.raw",        -1},
        {SFX"Rotation.raw",     -1},
        {SFX"Tetris.raw",       -1},
        {SFX"Win.raw",  	-1},
};

/**
 * Sets an sfx wating for playing, resetting any non fully played previous sfx
 */
void play_sfx(enum sfx fx) {
	if (-1 != game.sfx)
		lseek(game.sfx, 0, SEEK_SET);

	game.sfx = sfx_file[fx].fd;
}

/**
 * Place the cursor at a given position
 * @param x Abscissa in [0,99]
 * @param y Ordinate in [0,99]
 */
inline void put_cur(int x, int y) {
	x++;
	y++;
	WRITE(0x1b);
	WRITE(0x5b);

	if (y / 10) {
		WRITE(y / 10 + '0');
	}
	WRITE((y % 10) + '0');

	WRITE(0x3b);

	if (x / 10) {
		WRITE(x / 10 + '0');
	}
	WRITE((x % 10) + '0');

	WRITE(0x48);
}

/**
 * Cleanups the terminal state, re-displays the cursor, restores the color.
 */
void cleanup() {
	WRITES(cnorm);
	WRITE('\n');
	WRITES(sgr0);
	WRITES(clear);
}	

/**
 * Writes a whitespace of a given background color
 * @param color Color to put at the current position
 */
void put_color(int color) {
	char block[] = "\033[22;30m\033[22;40m \033[m\x0f";
	char black[] = "\033[m\x0f ";
	char *seq = color ? block : black;

	if (color) {
		seq[6] = (char)('0' + color);
		seq[14] = seq[6];
	}

	WRITES(seq);
}

/**
 * Hides the next piece indicator
 */
void hide_next() {
	int x = 0;
	int y = 0;

	for (x = 14; x < 18; x++) {
		for (y = 13; y < 17; y++) {
			put_cur(x, y);
			put_color(0);
		}
	}
}

/**
 * Re-draws or hides all the board.
 * @param hide If true, the board content is masked, otherwise it is refreshed
 */
void refresh_board(int hide) {
	int x = 0;
	int y = 0;

	for (x = 1; x < 11; x++) {
		for (y = 0; y < 18; y++) {
			put_cur(x, y);
			if (hide)
				put_color(5);
			else {
				if (' ' == board[y][x])
					put_color(0);
				else
					put_color(board[y][x] - '0');
			}
		}
	}
}

/**
 * Writes a number at a givent position, starting from it's right
 * @param x Abscissa where the number will be written to, starting from the
 * right
 * @param y Ordinate where the number will be written to
 * @param number Number to write
 */
void print_number(int x, int y, int number) {
	if (0 == number) {
		put_cur(x, y);
		WRITE('0');
	} else {
		while (number) {
			put_cur(x--, y);
			WRITE('0' + (number % 10));
			number /= 10;
		}
	}
}

/**
 * Writes all the board, except the playground
 */
void print_board() {
	int x = 0;
	int y = 0;

	for (x = 0; x < WIDTH; x++) {
		for (y = 0; y < HEIGHT; y++) {
			put_cur(x, y);
			if ('*' == board[y][x])
				put_color(7);
			else
				WRITE(board[y][x]);
		}
	}

	print_number(17, 7, game.lvl);
	refresh_board(0);
}

/**
 * Custom implementation of the libc time()
 * @param t If non-NULL, store the returned value
 * @return Number of elapsed seconds elapsed since the epoch
 */
time_t time(time_t *t) {
	struct timeval tv;
	int ret;

	ret = gettimeofday(&tv, NULL);
	if (-1 == ret)
		return (time_t)-1;

	if (NULL != t)
		*t = tv.tv_sec;

	return tv.tv_sec;
}

/**
 * Pseudo-random generator
 * @param seed if 0, ask for a number, else, set the new seed
 * @return Next number generated, in [0, 2^30)
 */
int my_random(int seed) {
	static int prev = 0;

	if (seed)
		prev = seed;

	prev = 1103515245 * prev + 12345;

	return prev & ((1 << 30) - 1);
}

/**
 * Draws/erases a piece
 * @param piece Piece to draw
 * @param ori Orientation of the piece
 * @param x Abscissa where to place the piece
 * @param y Ordinate where to place the piece
 * @param draw if 0, erases the piece, otherwise, draws it
 */
void draw_piece(int piece, int ori, int x, int y, int draw) {
	int i, j;

	for (i = 0; i < 4; i++)
	       for (j = 0; j < 4; j++)
		       if (PIXEL_LIT(GET_IMG(scale, piece, ori), i, j)) {
				put_cur(x + i, y + j);
				put_color(draw ? piece + 1 : 0);
		       }
}

/**
 * Draws / erase the current piece in the play ground
 * @param draw 0 to erase, non-zero to draw it
 */
void draw_current_piece(int draw) {
	draw_piece(current.piece, current.ori, 1 + current.x, current.y, draw);
	put_cur(80, 80);
}

/**
 * Draws / erase the next piece in the next piece indicator
 * @param draw 0 to erase, non-zero to draw it
 */
void draw_next_piece(int draw) {
	draw_piece(current.next_piece, 0, 14, 13, draw);
}

/**
 * Adds the piece to the play ground, once it has hit
 */
void fix_piece() {
	int i, j;

	for (i = 0; i < 4; i++)
		for (j = 0; j < 4; j++)
			if (PIXEL_LIT(GET_IMG(scale, current.piece, current.ori), i, j))
				board[current.next_y + j][1 + current.next_x + i] = (char)('1' + current.piece);
}

/**
 * Check wether the piece at the next position, collides with fixed block or not
 * @return 1 if the next position is possible, otherwise 0
 */
int can_move() {
	int i, j;

	for (i = 0; i < 4; i++)
		for (j = 0; j < 4; j++)
			if (PIXEL_LIT(GET_IMG(scale, current.piece, current.next_ori), i, j) &&
					' ' != board[current.next_y + j][1 + current.next_x + i])
				return 0;

	return 1;
}

/**
 * Effectively make the piece move, by setting the next position as the current
 * one
 */
void move() {
	if (game.music) {
		if (current.ori != current.next_ori)
			play_sfx(SFX_ROTATION);
		else if (current.x != current.next_x)
			play_sfx(SFX_MOVE);
	}

	draw_current_piece(0);
	current.x =   current.next_x;
	current.y =   current.next_y;
	current.ori = current.next_ori;
	draw_current_piece(1);
}

/**
 * Cancels the move if it would collide fixed blocks
 */
void cancel_move() {
	if (current.next_y != current.y) {
		current.hit = 1;
		current.next_y = current.y;
	} else {
		current.next_x = current.x;
		current.next_ori = current.ori;
	}
}

/**
 * Performs a move if it is possible, othewise, cancel it
 * @return 1 if the move is valid, 0 otherwise
 */
int try_move() {
	int moved = can_move();

	if (moved)
		move();
	else
		cancel_move();

	return moved;
}

/**
 * Makes the current piece try to go down of one step
 * @return 1 if the move is valid, 0 otherwise
 */
int down() {
	current.next_y++;
	return try_move();
}

/**
 * Chooses the next piece and updates the next piece indicator
 */
void get_next() {
	draw_next_piece(0);
	current.piece =      current.next_piece;
	current.next_piece = my_random(0) % 7;
	current.x =          3;
	current.next_x =     3;	
	current.y =          0;
	current.next_y =     0;	
	current.ori =        0;
	current.next_ori =   0;	
	if (0 == current.piece) {
		current.y--;
		current.next_y--;
	}
	draw_next_piece(1);
}

/**
 * For two player (lan) mode, when a player has cleared more than one
 * line at once, adds the other penality lines.
 * @param pending_lines Number of lines to add at the bottom of the board
 */
void add_lines(int pending_lines) {
	int i, j;

	for (j = 0; j < 18 - pending_lines; j++)
		for (i = 1; i < 11; i++)
			board[j][i] = board[j + pending_lines][i];

	for (j = 18 - pending_lines; j < 18; j++)
		for (i = 1; i < 11; i++)
			board[j][i] = i != game.void_col ? '1' : ' ';

	refresh_board(0);
}

/**
 * In 2 player mode, updates the gauge showing the height of the other player
 * @param value Height reached by the other player
 */
void update_gauge(int value) {
	int j;

	for (j = 0; j < value; j++) {
		put_cur(18, j);
		put_color(7);
	}
	for (; j < 18; j++) {
		put_cur(18, j);
		put_color(3);
	}
}

/**
 * Sends a 2 player network message to the remote peer
 * @param code Code of the tetris message to send
 * @param value Value associated, used only by MSG_LINES and
 * MSG_HEIGHT, ignored otherwise
 * @return -1 in case of error, otherwise a positive value
 */
int send_msg(int code, int value) {
	if (net.mode) {
		char msg = MSG_BUILD(code, value);

		return write(net.fd, &msg, sizeof(char));
	}

	return -1;
}

/**
 * Hides the board and prints a message of 10 char max, with given for and
 * background colors
 * @param msg Message of 10 chars max
 * @param fore Foreground color
 * @param back Background color
 */
void print_msg(char *msg, int fore, int back) {
	char f = (char)(MAX(0, MIN(7, fore)) + '0');
	char b = (char)(MAX(0, MIN(7, back)) + '0');

	refresh_board(1);
	put_cur(1, 5);
	WRITES("\033[30;3");
	WRITE(f);
	WRITES("m\033[22;4");
	WRITE(b);
	WRITES("m          ");

	put_cur(1, 6);
	WRITES(msg);

	put_cur(1, 7);
	WRITES("          \033[m\x0f");
}

/**
 * Suspends/restarts the game, and prints a pause message
 */
void in_pause() {
	game.pause = !game.pause;
	if (game.pause) {
		play_sfx(SFX_PAUSE);
		print_msg("* pause! *", 5, 3);
		hide_next();
	} else {
		refresh_board(0);
		draw_current_piece(1);
		draw_next_piece(1);
	}
}

/**
 * Checks if the remote sent a message and acts accordingly
 * @param pending_lines Number of penalty lines, updated in output if the remote
 * did multiples lines
 * @param loop in output, set to 0 if a message which stops the game has been
 * received, i.e. QUIT or LOST
 * @return -1 in case of error, otherwise 0
 */
int read_msg(int *pending_lines, int *loop, char *msg) {
	int ret = -1;

	ret = read(net.fd, msg, 1);
	switch (ret) {
		case -1:
			if (errno != EAGAIN) {
				WRITES("error : read");
				return -1;
			}
			break;

		case 1:
			switch (MSG_CODE(*msg)) {
				case MSG_HEIGHT:
					put_cur(25, 25);
					update_gauge(MSG_VALUE(*msg));
					break;

				case MSG_LINES:
					*pending_lines += MSG_VALUE(*msg);
					break;

				case MSG_LOST:
					*loop = 0;
					game.status = END_WON;
					play_sfx(SFX_WIN);
					break;

				case MSG_QUIT:
					*loop = 0;
					game.status = END_PEER_LEFT;
					break;

				case MSG_PAUSE:
					in_pause();
					break;

				default:
					WRITES("error : Unknown message\n");
					return -1;
					break;
			}
			break;

		default:
			break;
	}

	return 0;
}

/**
 * Computes the current game's height and sends it to the remote
 */
void update_height() {
	int i, j;
	int old_height = game.height;

	game.height = 0;

	for (j = 17; j >= 0; j--)
		for (i = 1; i < 11; i++)
			if (' ' != board[j][i])
				game.height = j;

	if (game.height != old_height)
		send_msg(MSG_HEIGHT, game.height);
}

/**
 * Deletes a given line which is known to have been completed. Updates and
 * prints the lines counter and the level and updates the game speed accordingly
 * @param line Index of the line to clear from top to bottom starting by 0
 */
void complete_line(int line) {
	int i;

	/* clear the line */
	while (line--)
		for (i = 1; i < 11; i++) {
			board[line + 1][i] = board[line][i];
			put_cur(i, line + 1);
			if (' ' == board[line + 1][i])
				put_color(0);
			else
				put_color(board[line + 1][i] - '0');
		}

	/* update lines, level and period (game speed) */
	if (game.mode == 'b') {
		game.lines--;
		if (0 > game.lines)
			game.lines = 0;
	} else {
		game.lines++;
		if (game.lines % 10 == 0) {
			int real_lvl = game.lines / 10;

			game.lvl = real_lvl > game.lvl ? real_lvl : game.lvl;
			print_number(17, 7, game.lvl);
		}
	}
	print_number(17, 11, game.lines);
	if (9 == game.lines) {/* quirk for erasing leading 1, when < 10 */
		put_cur(16, 11);
		WRITE(' ');
	}
	game.period = INITIAL_PERIOD - 2 * game.lvl;
}

/**
 * Check if one or more lines have been completed. If there are some, cleans
 * them, updates the score and send a message to the remote in case of multiple
 * lines.
 */
void check_lines() {
	int i, j;
	int line_complete = 1;
	int total = 0;
	int coef[5] = {0, 40, 100, 300, 1200};

	for (j = 0; j < 18; j++) {
		for (i = 1; i < 11; i++) {
			if (board[j][i] == ' ') {
				line_complete = 0;
				break;
			}
		}
		if (line_complete) {
			complete_line(j);
			total++;
		}
		line_complete = 1;
	}

	/* play the right sfx */
	if (total != 0) {
		if (4 == total)
			play_sfx(SFX_TETRIS);
		else
			play_sfx(SFX_LINE);
		game.suspended = 60;
	}

	/* update score */
	game.score += coef[total] * (game.lvl + 1);
	if ('b' != game.mode) {
		print_number(17, 3, game.score);
	}

	if (net.mode && total > 1)
		send_msg(MSG_LINES, total - 1);
}

/**
 * Crappy but funny implementation of strlen
 * @param s String we want the length of
 * @return Length of the string
 */
size_t strlen(const char *s) {
	return '\0' == *s ? 0 : 1 + strlen(s + 1);
}

/**
 * Adds random blocks to the grid, up to game's high factor, with a probability
 * of 7/20.
 */
void add_crumbles() {
	int i, j;
	char lut[] = "1234567             ";
	int limit = 17 - 2 * game.high;
	int mod = (int)strlen(lut);

	for (j = 17; j > limit; j--)
		for (i = 1; i < 11; i++)
			board[j][i] = lut[my_random(0) % mod];
}

/**
 * Prints the content of a file to standard output
 * @param file Path of the file to dump
 */
void dump_file(const char *file) {
	int fd = -1;
	char buf[10];
	ssize_t bytes_read;

	fd = open(file, O_RDONLY, 0);
	if (-1 == fd) {
		WRITES("Can't find \"");
		WRITES(file);
		WRITES("\" file\n");
	}
	else
		while (0 < (bytes_read = read(fd, buf, 10)))
			write(1, buf, (size_t)bytes_read);
}

/**
 * Prints a little help about command line invocation
 */
void usage() {
	dump_file("usage");
}

/**
 * Prints the keys used in the game
 */
void help() {
	dump_file("keys");
}

/**
 * Implementation of standard C library's atoi
 * @param nptr String containing the number
 * @return nptr Converted to a number in base 10
 */
int atoi(char *nptr) {
	int ret = 0;

	while (*nptr >= '0' && *nptr <= '9') {
		ret += *nptr - '0';
		nptr++;
	}

	return ret;
}

/**
 * Reads the port number from a string
 * @param port String containing the port number
 * @return Port read, in [1025,65536]
 */
int read_port(char *port) {
	int ret;

	ret = atoi(port);
	if (0 == ret)
		ret = NET_DEFAULT_PORT;
	else
		ret = MIN(1025, MAX(ret, 65536));

	return ret;
}

/**
 * Configures the server in a 2 player game
 * @return -1 in case of error, otherwise 0
 */
int set_up_server() {
	int ret = -1;
	struct sockaddr_in sin;
	struct sockaddr_in csin;
	int yes = 1;
	socklen_t len = sizeof(csin);
	int flags = 0;

	WRITES("Server mode\n");

	net.sfd = socket(AF_INET, SOCK_STREAM, 0);
	if (-1 == net.sfd) {
		WRITES("Can't create network socket\n");
		return -1;
	}
	/* Configure to allow reuse */
	ret = setsockopt(net.sfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
	if (-1 == ret) {
		WRITES("error : setsockopt\n");
		return 1;
	}
	sin.sin_addr.s_addr = htonl(INADDR_ANY);
	sin.sin_family = AF_INET;
	sin.sin_port = htons((uint16_t)net.port);
	ret = bind(net.sfd, (struct sockaddr*)(&sin), sizeof(sin));
	if (-1 == ret) {
		WRITES("error : bind\n");
		return -1;
	}
	ret = listen(net.sfd, 1);
	if (-1 == ret) {
		WRITES("error : listen\n");
		return -1;
	}
	WRITES("Waiting for connection\n");
	net.fd = accept(net.sfd, (struct sockaddr *)(&csin), &len);
	if (-1 == net.fd) {
		WRITES("error : accept\n");
		return -1;
	}
	WRITES("A client has connected\n");

	flags = fcntl(net.fd, F_GETFL);
	if (-1 == flags)
		return -1;
	ret = fcntl(net.fd, F_SETFL, flags|O_NONBLOCK);
	if (-1 == ret)
		return -1;

	return 0;
}

/**
 * Configures the client in a 2 player game
 * @return -1 in case of error, otherwise 0
 */
int set_up_client() {
	int ret = -1;
	struct sockaddr_in sin;
	int flags = -1;

	sin.sin_addr.s_addr = inet_addr(net.addr);
	sin.sin_family = AF_INET;
	sin.sin_port = htons((uint16_t)net.port);

	net.fd = socket(AF_INET, SOCK_STREAM, 0);
	if (-1 == net.fd) {
		WRITES("error : socket\n");
		return -1;
	}
	WRITES("Connection to ");
	WRITES(net.addr);
	WRITE('\n');
	ret = connect(net.fd, (struct sockaddr *)(&sin), sizeof(sin));
	if (-1 == ret) {
		WRITES("error : connect\n");
		return -1;
	}
	WRITES("Connected to the server\n");
	flags = fcntl(net.fd, F_GETFL);
	if (-1 == flags)
		return -1;
	ret = fcntl(net.fd, F_SETFL, flags|O_NONBLOCK);
	if (-1 == ret)
		return -1;

	return 0;
}

/**
 * Process level and high arguments. Uses only the first digit
 * @param argc Number of remaining arguments
 * @param argv Array of the remaining arguments, contains normally only level
 * and optionnally high
 */
void process_lvl_high_args(int argc, char *argv[]) {
	game.lvl = argv[0][0] - '0';
	if (argc > 1)
		game.high = argv[1][0] - '0';

	/* check args */
	if (game.high > 5 || game.high < 0)
		game.high = 0;

	if (game.lvl > 9 || game.lvl < 0)
		game.lvl = 0;

	game.period = INITIAL_PERIOD - 2 * game.lvl;
}

/**
 * Processes command-line arguments
 * @param argc Number of arguments
 * @param argv Array of the command-line arguments
 */
void process_args(int argc, char *argv[]) {
	if (argc != 1) {
		game.mode = argv[1][0];
		switch (game.mode) {
		case 'a':
		case 'b':
			if (argc >= 3)
				process_lvl_high_args(argc - 2, argv + 2);
			// game.lines = 2;
			game.lines = 25;
			break;

		case '2':
			if (argc < 3) {
				usage();
				_exit(1);
			}
			if (':' == argv[2][0]) {
				net.mode = NET_SERVER;
				net.port = read_port(argv[2] + 1);
			} else {
				char *p = NULL;

				WRITES("Client mode\n");
				net.mode = NET_CLIENT;
				p = net.addr = argv[2];
				for (; ':' != *p && '\0' != *p; p++);
				if (*p != ':') {
					usage();
					_exit(1);
				}
				*p = '\0';
				net.port = read_port(p + 1);
			}
			if (argc >= 4)
				process_lvl_high_args(argc - 3, argv + 3);
			game.void_col = 1 + (my_random(0) % 10);
			break;

		case 'h':
			help();
		default:
			usage();
			_exit(0);
			break;
		}
	}

	add_crumbles(game.high);
}

/**
 * Configures the network for a 2 player game
 * @return 0 in case of success, otherwise 0
 */
int config_network() {
	int ret;

	if (net.mode == NET_SERVER)
		ret = set_up_server();
	else if (net.mode == NET_CLIENT)
		ret = set_up_client();

	return ret;
}

/**
 * Fixes a piece which has it, gets the next piece, checks the lines
 * see if the player has lost, adds penalty lines
 */
void piece_hit() {
	play_sfx(SFX_DROP);
	fix_piece();
	get_next();
	current.hit = 0;
	check_lines();
	draw_current_piece(1);
	if (!can_move()) {
		game.loop = 0;
		game.status = END_LOST;
		send_msg(MSG_LOST, 0);
		play_sfx(SFX_LOST);
	}
	if (net.pending_lines) {
		add_lines(net.pending_lines);
		net.pending_lines = 0;
	}

	if (net.mode)
		update_height();

	game.freeze = 10;
}

struct termios old_tios;

/**
 * Configures input/and output
 * @return 1 in case of error, 0 otherwise
 */
int config_io(void) {
	int fd_flags;
	int ret;
	struct termios new_tios;

	ret = tcgetattr(0, &old_tios);
	if (-1 == ret) {
		WRITES("error : tcgetattr");
		return -1;
	}
	cfmakeraw(&new_tios);
	ret = tcsetattr(0, TCSANOW, &new_tios);
	if (-1 == ret) {
		WRITES("error : tcsetattr");
		return -1;
	}

	fd_flags = fcntl(0, F_GETFL);
	if (-1 == fd_flags)
		return -1;
	ret = fcntl(0, F_SETFL, fd_flags|O_NONBLOCK);
	if (-1 == ret)
		return -1;

	WRITES(civis);
	WRITES(clear);
	print_board();

	return 0;
}

/**
 * Restores the state of the terminal
 */
void restore_io() {
	char key;

	/* flush non processed characters */
	while (1 == read(0, &key, 1));

	/* restore terminal */
	tcsetattr(0, TCSANOW, &old_tios);

	put_cur(0, 0);
	cleanup();
}

/**
 * Performs actions on keystrokes.
 * @param key Key pressed this frame
 * @return 1 if a move has been performed
 */
int check_keys(int key) {
	int moved_down = 0;
	char buf;

	if ('p' == key || '\r' == key) {
		/* start */
		send_msg(MSG_PAUSE, 0);
		in_pause();
	} else if (!game.pause) {
		switch (key) {
		case 'j':
			/* left */
			current.next_x--;
			try_move();
			break;

		case 'k':
			/* down */
			if (!game.freeze)
				moved_down = down();
			break;

		case 'l':
			current.next_x++;
			try_move();
			break;

		case 'f':
		case 'i':
			/* A */
			current.next_ori++;
			if (!VALID_IMG(GET_IMG(scale, current.piece,
						       	current.next_ori)))
				current.next_ori = 0;
			try_move();
			break;

		case 'd':
		case 'u':
			/* B */
			current.next_ori--;
			if (current.next_ori < 0)
				current.next_ori = 4;
			while (!VALID_IMG(GET_IMG(scale, current.piece,
						       	current.next_ori)))
				current.next_ori--;
			try_move();
			break;

		case ' ':
			/* select */
			break;

		case 0x1b: /* ESC */
			/* quit */
			if (read(0, &buf, 1) == 1)
				break;
			game.status = END_QUIT;
			game.loop = 0;
			if (net.mode)
				send_msg(MSG_QUIT, 0);
			break;

		default:
#if 0			/* debug */
			if (key)
				print_number(25, 25, key);
#endif
			break;
		}
	}

	return moved_down;
}


/**
 * Frees network related resources
 */
void close_net()  {
	if (-1 != net.sfd)
		close(net.sfd);
	close(net.fd);
}

/**
 * Displays the result of the game
 * @param msg Last network message received
 */
void display_result(char msg) {
	switch (game.status) {
		case END_WON:
			print_msg(" YOU WON !", 4, 2);
			break;

		case END_LOST:
			print_msg("LOOSER !!!", 4, 1);
			break;

		case END_PEER_LEFT:
			print_msg("PEER LEFT ", 4, 3);
			break;

		case END_QUIT:
			print_msg("BYE BYE !!", 4, 3);
			break;

		default:
			/* can't happen */
			break;
	}

	usleep(2000000);
}

int config_music() {
	int ret = -1;
	int rate = 44100;
	int channels = 2;
	int i;

	game.dsp = open("/dev/dsp", O_RDWR);
	game.bgm = open("sound/bgm.raw", O_RDONLY);
	if (-1 == game.dsp) {
		WRITES("error : open dsp\n");
		return 0;
	}
	if (-1 == game.bgm) {
		WRITES("error : open bgm\n");
		return 0;
	}
	/* set samplerate */
	ret = ioctl(game.dsp, SNDCTL_DSP_SPEED, &rate);
	if (-1 == ret) {
		WRITES("error : ioctl samplerate\n");
		return 0;
	}
	/* set stereo */
	ret = ioctl(game.dsp, SNDCTL_DSP_CHANNELS, &channels);
	if (-1 == ret) {
		WRITES("error : ioctl stereo\n");
		return 0;
	}

	for (i = 0; i < SFX_NB; i++) {
		sfx_file[i].fd = open(sfx_file[i].path, O_RDONLY);
		if (sfx_file[i].fd == -1) {
			WRITES("error : opening ");
			WRITES(sfx_file[i].path);
			WRITE('\n');
		}
	}
	
	return 1;
}

/**
 * Loads the bgm and sfx chunks, mix them together and send the to the sound
 * card
 */
void update_music() {
	int ret_bgm = -1;
	int ret_sfx = -1;
	unsigned char buf_bgm[BUF_SIZE] = {0};
	unsigned char buf_sfx[BUF_SIZE];
	int i = 0;

	/* read bgm */
	if (0 || game.pause || !game.loop) {
		ret_bgm = BUF_SIZE;
		for  (i = 0; i < BUF_SIZE; i++)
			buf_bgm[i] = 128;
	} else
		ret_bgm = read(game.bgm, buf_bgm, BUF_SIZE);
	if (-1 == ret_bgm && errno == EAGAIN)
		return;
	if (-1 == ret_bgm)
		WRITES("error : read\n");
	if (ret_bgm <= 0) {
		lseek(game.bgm, 0, SEEK_SET);
		ret_bgm = read(game.bgm, buf_bgm, BUF_SIZE);
		if (-1 == ret_bgm)
			WRITES("error : read\n");
	}
	if (ret_bgm > 0) {
		/* read sfx and mix it with the bgm chunk */
		if (-1 != game.sfx) {
			size_t sfx_chunk_len = (size_t)MIN(ret_bgm, BUF_SIZE);

			ret_sfx = read(game.sfx, buf_sfx, sfx_chunk_len);
			if (0 == ret_sfx) {
				lseek(game.sfx, 0, SEEK_SET);
				game.sfx = -1;
			}
			else if (-1 != game.sfx)
				for (i = 0; i < ret_sfx; i++)
					buf_bgm[i] = (unsigned char)(buf_bgm[i] + buf_sfx[i] - 128);
		}

		/* play the result */
		ret_bgm = write(game.dsp, buf_bgm, (size_t)ret_bgm);
		if (-1 == ret_bgm)
			WRITES("error : write\n");
	}
}

/**
 * Make the inter-frame duration nearly constant, by sleeping until it lasts
 * INTER_FRAME
 */
void smooth_time(struct timeval tv, struct timeval old_tv) {
	suseconds_t udiff;

	if (tv.tv_usec < old_tv.tv_usec)
		udiff = 1000000 + tv.tv_usec - old_tv.tv_usec;
	else
		udiff = tv.tv_usec - old_tv.tv_usec;

	if (udiff < INTER_FRAME)
		usleep((useconds_t)(INTER_FRAME - udiff));
}

int main(int argc, char *argv[]) {
	char key = 0;
	int ret;
	int frame = 0;
	char msg;
	int moved_down = 0;
	struct timeval old_tv, tv;

	/* init pseudo-random generator */
	my_random(time(NULL));

	process_args(argc, argv);

	ret = config_network();
	if (-1 == ret)
		goto out;

	game.music = config_music();
	if (game.music)
		WRITES("Music enabled\n");
	else
		WRITES("Music disabled\n");

	ret = config_io();
	if (-1 == ret)
		goto out;

	current.next_piece = my_random(0) % 7;
	get_next();
	draw_current_piece(1);

	usleep(1000000);

	gettimeofday(&old_tv, NULL);
	while (game.loop || -1 != game.sfx) {
		gettimeofday(&tv, NULL);
		smooth_time(tv, old_tv);
		gettimeofday(&old_tv, NULL);

		if (game.loop && END_NONE == game.status && !game.suspended) {
			if (read(0, &key, 1));
			if (key) /* TODO handle read errors */
				moved_down = check_keys(key);
			if (frame >= game.period)
				moved_down |= down();
			if (moved_down)
				frame = 0;
			moved_down = 0;

			if (current.hit)
				piece_hit();

			if (game.freeze)
				game.freeze--;
			if (!game.pause)
				frame++;
			key = 0;
			if (0 >= game.lines && 'b' == game.mode) {
				play_sfx(SFX_WIN);
				game.loop = 0;
				game.status = END_WON;
			}

			if (net.mode)
				read_msg(&net.pending_lines, &game.loop, &msg);
		}
		/* flush acculated keypresses while suspended */
		if (1 == game.suspended)
			while (read(0, &key, 1) != -1);

		if (game.suspended)
			game.suspended--;
		if (game.music)
			update_music();
	}

	display_result(msg);

	if (net.mode)
		close_net();

	restore_io();

	return 0;
out:
	return 1;
}

#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>

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
static const char cnorm[] = {0x1b, 0x5b, 0x33, 0x34, 0x68, 0x1b, 0x5b, 0x3f, 0x32, 0x35, 0x68, 0};

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
static char board[19][19] = {
	{'*', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', '*', '*', '*', '*', '*', '*', '*', '*'},
	{'*', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', '*', 's', 'c', 'o', 'r', 'e', '*', '*'},
	{'*', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', '*', '*', '*', '*', '*', '*', '*', '*'},
	{'*', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', '*', ' ', ' ', ' ', ' ', ' ', '0', '*'},
	{'*', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', '*', '*', '*', '*', '*', '*', '*', '*'},
	{'*', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', '*', 'l', 'e', 'v', 'e', 'l', '*', '*'},
	{'*', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', '*', '*', '*', '*', '*', '*', '*', '*'},
	{'*', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', '*', ' ', ' ', ' ', ' ', ' ', '0', '*'},
	{'*', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', '*', '*', '*', '*', '*', '*', '*', '*'},
	{'*', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', '*', 'l', 'i', 'n', 'e', 's', '*', '*'},
	{'*', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', '*', '*', '*', '*', '*', '*', '*', '*'},
	{'*', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', '*', ' ', ' ', ' ', ' ', ' ', '0', '*'},
	{'*', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', '*', '*', '*', '*', '*', '*', '*', '*'},
	{'*', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', '*', '*', '*', ' ', ' ', ' ', ' ', '*'},
	{'*', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', '*', '*', '*', ' ', ' ', ' ', ' ', '*'},
	{'*', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', '*', '*', '*', ' ', ' ', ' ', ' ', '*'},
	{'*', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', '*', '*', '*', ' ', ' ', ' ', ' ', '*'},
	{'*', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', '*', '*', '*', '*', '*', '*', '*', '*'},
	{'*', '*', '*', '*', '*', '*', '*', '*', '*', '*', '*', '*', '*', '*', '*', '*', '*', '*', '*'},
};

/**
 * \var game
 * \brief Main structure representing the game's current state
 */
struct {
	char mode;  /**< Game mode, 'a', 'b' or '2' (for two players) */
	int high;   /**< Height of the handicap */
	int level;  /**< Speed of pieces dropping */
	int lines;  /**< Number of lines achieved so far */
	int score;  /**< Current score of the player */
	int period; /**< Number of loop passes between two automatic drops */
} game = {
		.mode =   'a',
		.high =   0,
		.level =  0,
		.lines =  0,
		.score =  0,
		.period = INITIAL_PERIOD,
};

typedef char image[4][4];

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
} net = {
		.mode = NET_NONE,
		.addr = "localhost",
		.port = NET_DEFAULT_PORT,
		.sfd = -1,
		.fd = -1,
};

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

/**
 * Place the cursor at a given position
 * @param x Abscissa
 * @param y Ordinate
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

void cleanup() {
	int stdin_flags;

	/* TODO put things in the right order */
	if (system("stty -raw echo"));
	WRITES(cnorm);
	stdin_flags = fcntl(0, F_GETFL);
	fcntl(0, F_SETFL, stdin_flags&(~O_NONBLOCK));
	WRITE('\n');
	WRITES(sgr0);
	WRITES(clear);
}	

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

	print_number(17, 7, game.level);
	refresh_board(0);
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

void draw_piece(int piece, int ori, int x, int y, int draw) {
	int i, j;

	for (i = 0; i < 4; i++)
	       for (j = 0; j < 4; j++)
		       if (' ' != (*((*(scale[piece]))[ori]))[j][i]) {
				put_cur(x + i, y + j);
				put_color(draw ? piece + 1 : 0);
		       }
}

void draw_current_piece(int draw) {
	draw_piece(current.piece, current.ori, 1 + current.x, current.y, draw);
}

void draw_next_piece(int draw) {
	draw_piece(current.next_piece, 0, 14, 13, draw);
}

void fix_piece() {
	int i, j;

	for (i = 0; i < 4; i++)
		for (j = 0; j < 4; j++)
			if (' ' != (*((*(scale[current.piece]))[current.next_ori]))[j][i])
				board[current.next_y + j][1 + current.next_x + i] = (char)('1' + current.piece);
}

int can_move() {
	int i, j;

	for (i = 0; i < 4; i++) {
		for (j = 0; j < 4; j++) {
			if (' ' != (*((*(scale[current.piece]))[current.next_ori]))[j][i] &&
				' ' != board[current.next_y + j][1 + current.next_x + i])
				return 0;
		}
	}

	return 1;
}

void move() {
	draw_current_piece(0);
	current.x =   current.next_x;
	current.y =   current.next_y;
	current.ori = current.next_ori;
	draw_current_piece(1);
}

void cancel_move() {
	if (current.next_y != current.y) {
		current.hit = 1;
		current.next_y = current.y;
	} else {
		current.next_x = current.x;
		current.next_ori = current.ori;
	}
}

void try_move() {
	if (can_move())
		move();
	else
		cancel_move();
}

void down() {
	current.next_y++;
	try_move();
}

void help() {
	char keys[] = "q\tQuit\n\
d\tTurn clockwise\n\
f\tTurn counter-clockwise\n\
j\tMove left\n\
k\tMove down\n\
l\tMove right\n";

	WRITES(keys);
}

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

int void_column = 0;

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
			board[j][i] = i != void_column ? '1' : ' ';

	refresh_board(0);
}

void send_msg(int code, int value) {
	if (net.mode) {
		char msg = MSG_BUILD(code, value);
		int ret;

		ret = write(NET_SERVER == net.mode ? net.fd : net.sfd, &msg, sizeof(char));
		if (-1 != ret && errno != EAGAIN) {
			perror("write");
			return;
		}
	}
}

int max_height = 0;

void update_height() {
	int i, j;
	int old_height = max_height;

	max_height = 0;

	for (j = 17; j >= 0; j--)
		for (i = 1; i < 11; i++)
			if (' ' != board[j][i])
				max_height = j;

	if (max_height != old_height)
		send_msg(MSG_HEIGHT, max_height);
}

void complete_line(int line) {
	int i;

	while (line--)
		for (i = 1; i < 11; i++) {
			board[line + 1][i] = board[line][i];
			put_cur(i, line + 1);
			if (' ' == board[line + 1][i])
				put_color(0);
			else
				put_color(board[line + 1][i] - '0');
		}
	if (game.mode == 'b') {
		game.lines--;
		if (0 > game.lines)
			game.lines = 0;
	} else {
		game.lines++;
		if (game.lines % 10 == 0) {
			int real_level = game.lines / 10;

			game.level = real_level > game.level ? real_level : game.level;
			print_number(17, 7, game.level);
		}
	}

	print_number(17, 11, game.lines);
	if (9 == game.lines) {/* quirk for erasing leading 1, when going under 10 */
		put_cur(16, 11);
		WRITE(' ');
	}
	game.period = INITIAL_PERIOD - 2 * game.level;
}


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

	/* update score */
	game.score += coef[total] * (game.level + 1);
	if ('b' != game.mode) {
		print_number(17, 3, game.score);
	}

	if (net.mode && total > 1)
		send_msg(MSG_LINES, total - 1);
}

void add_crumbles() {
	int i, j;
	char lut[20] = {'1', '2', '3', '4', '5', '6', '7',
		' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
		' ', ' ', ' ', ' ', ' '};
	int limit = 17 - 2 * game.high;

	for (j = 17; j > limit; j--)
		for (i = 1; i < 11; i++)
			board[j][i] = lut[my_random(0) % 20];
}

int suspend = 0;

void in_pause() {
	suspend = !suspend;
	if (suspend) {
		refresh_board(1);
		hide_next();
		put_cur(1, 6);
		WRITES("\033[22;35m\033[22;43m* pause! *\033[m\x0f");
	} else {
		refresh_board(0);
		draw_current_piece(1);
		draw_next_piece(1);
	}
}

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
 * Prints a little help about command line invocation
 */
void usage() {
	int fd = -1;
	char buf[10];
	ssize_t bytes_read;

	fd = open("usage", O_RDONLY, 0);
	if (-1 == fd)
		WRITES("Can't find \"usage\" file\n");
	else
		while (0 < (bytes_read = read(fd, buf, 10)))
			write(1, buf, (size_t)bytes_read);
}

int main(int argc, char *argv[]) {
	char key = 0;
	int stdin_flags;
	int ret;
	int loop = 1;
	int frame = 0;
	int freeze_down = 0;
	char msg;
	struct termios old_tios, new_tios;
	int pending_lines = 0;

	my_random(time(NULL));

	/* args processing */
	if (argc != 1) {
		game.mode = argv[1][0];
		switch (game.mode) {
		case 'a':
		case 'b':
			if (argc >= 3)
				game.level = argv[2][0] - '0';
			if (argc >= 4)
				game.high = argv[3][0] - '0';
			if (game.high > 5 || game.high < 0)
				game.high = 0;
			game.lines = 25;
			add_crumbles(game.high);
			break;

		case '2':
			void_column = 1 + (my_random(0) % 10);
			if (argc < 3) {
				usage();
				return 1;
			}
			if (':' == argv[2][0]) {
				struct sockaddr_in sin;
				struct sockaddr_in csin;

				printf("Server mode\n");
				net.mode = NET_SERVER;
				net.port = atoi(argv[2] + 1);
				net.port = MIN(1025, MAX(net.port, 65536));

				net.sfd = socket(AF_INET, SOCK_STREAM, 0);
				if (-1 == net.sfd) {
					perror("Can't create network socket");
					return 1;
				}
				/* Configure to allow reuse */
				int yes = 1;
				ret = setsockopt(net.sfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
				if (-1 == ret) {
					perror("setsockopt");
					return 1;
				}
				sin.sin_addr.s_addr = htonl(INADDR_ANY);
				sin.sin_family = AF_INET;
				sin.sin_port = htons((uint16_t)net.port);
				ret = bind(net.sfd, (struct sockaddr*)(&sin), sizeof(sin));
				if (-1 == ret) {
					perror("bind");
					return 1;
				}
				ret = listen(net.sfd, 1);
				if (-1 == ret) {
					perror("listen");
					return 1;
				}
				socklen_t len = sizeof(csin);
				printf("Waiting for connection\n");
				net.fd = accept(net.sfd, (struct sockaddr *)(&csin), &len);
				if (-1 == net.fd) {
					perror("accept");
					return 1;
				}
				printf("A client has connected\n");
				/* TODO rename stdin_flags */
				stdin_flags = fcntl(net.fd, F_GETFL);
				if (-1 == stdin_flags)
					goto out;
				ret = fcntl(net.fd, F_SETFL, stdin_flags|O_NONBLOCK);
				if (-1 == ret)
					goto out;
			} else {
				char *p = NULL;
				struct sockaddr_in sin;

				printf("Client mode\n");
				net.mode = NET_CLIENT;
				p = net.addr = argv[2];
				for (; ':' != *p && '\0' != *p; p++);
				if (*p != ':') {
					usage();
					return 1;
				}
				*p = '\0';
				net.port = atoi(p + 1);
				if (0 == net.port)
					net.port = NET_DEFAULT_PORT;
				else
					net.port = MIN(1025, MAX(net.port, 65536));

				sin.sin_addr.s_addr = inet_addr(net.addr);
				sin.sin_family = AF_INET;
				sin.sin_port = htons((uint16_t)net.port);

				net.sfd = socket(AF_INET, SOCK_STREAM, 0);
				if (-1 == net.sfd) {
					perror("socket");
					return 1;
				}
				printf("Connection to %s:%d\n", net.addr, net.port);
				ret = connect(net.sfd, (struct sockaddr *)(&sin), sizeof(sin));
				if (-1 == ret) {
					perror("connect");
					return 1;
				}
				printf("Connected to the server\n");
				stdin_flags = fcntl(net.sfd, F_GETFL);
				if (-1 == stdin_flags)
					goto out;
				ret = fcntl(net.sfd, F_SETFL, stdin_flags|O_NONBLOCK);
				if (-1 == ret)
					goto out;
			}
			if (argc >= 4) {
				game.level = argv[3][0] - '0';
				if (argc >= 5)
					game.high = argv[4][0] - '0';
				if (game.high > 5 || game.high < 0)
					game.high = 0;
				game.lines = 25;
				add_crumbles(game.high);
			}
			break;

		default:
			usage();
			return 0;
		}
	}
	
	ret = tcgetattr(0, &old_tios);
	if (-1 == ret) {
		perror("tcgetattr");
		return 1;
	}
	cfmakeraw(&new_tios);
	ret = tcsetattr(0, TCSANOW, &new_tios);
	if (-1 == ret) {
		perror("tcsetattr");
		return 1;
	}

	/* check args */
	if (game.level > 9 || game.level < 0)
		game.level = 0;

	game.period = INITIAL_PERIOD - 2 * game.level;

	stdin_flags = fcntl(0, F_GETFL);
	if (-1 == stdin_flags)
		goto out;
	ret = fcntl(0, F_SETFL, stdin_flags|O_NONBLOCK);
	if (-1 == ret)
		goto out;

	WRITES(civis);
	WRITES(clear);
	print_board();
	current.next_piece = my_random(0) % 7;
	get_next();
	
	draw_current_piece(1);

	while (loop) {
		/* TODO manage errors in read... or not... */
		if (read(0, &key, 1));
		
		usleep(20000);
		switch (key) {
		case 'j':
			/* left */
			current.next_x--;
			try_move();
			break;

		case 'k':
			/* down */
			if (!freeze_down)
				down();
			frame = 0;
			break;

		case 'l':
			current.next_x++;
			try_move();
			break;

		case 'f':
		case 'i':
			/* A */
			current.next_ori++;
			if (NULL == (*(scale[current.piece]))[current.next_ori])
				current.next_ori = 0;
			try_move();
			break;

		case 'd':
		case 'u':
			/* B */
			current.next_ori--;
			if (current.next_ori < 0)
				current.next_ori = 4;
			while (NULL == (*(scale[current.piece]))[current.next_ori])
				current.next_ori--;
			try_move();
			break;

		case ' ':
			/* select */
			break;

		case '\r':
			/* start */
			send_msg(MSG_PAUSE, 0);
			in_pause();
			break;

		case 'q':
			/* quit */
			loop = 0;
			if (net.mode)
				send_msg(MSG_QUIT, 0);
			break;
		}

		if (current.hit) {
			fix_piece();
			get_next();
			current.hit = 0;
			check_lines();
			draw_current_piece(1);
			if (!can_move()) {
				loop = 0;
				send_msg(MSG_LOST, 0);
			}
			if (pending_lines) {
				add_lines(pending_lines);
				pending_lines = 0;
			}

			if (net.mode)
				update_height();

			usleep(100000);
			freeze_down = 10;
		}
		if (frame >= game.period) {
			frame = 0;
			down();
		}
		if (freeze_down)
			freeze_down--;
		if (!suspend)
			frame++;
		key = 0;
		if (0 >= game.lines && 'b' == game.mode) {
			usleep(2000000);
			loop = 0;
		}

		if (net.mode) {
			/* TODO check lan messages */
			ret = read(NET_SERVER == net.mode ? net.fd : net.sfd, &msg, 1);
			switch (ret) {
				case -1:
					/* TODO error */
					if (errno != EAGAIN)
						perror("write");
					break;

				case 1:
					switch (MSG_CODE(msg)) {
						case MSG_HEIGHT:
							put_cur(25, 25);
							update_gauge(MSG_VALUE(msg));
							break;

						case MSG_LINES:
							pending_lines += MSG_VALUE(msg);
							break;

						case MSG_LOST:
							loop = 0;
							break;

						case MSG_QUIT:
							loop = 0;
							break;

						case MSG_PAUSE:
							in_pause();
							break;

						default:
							fprintf(stderr, "Unknown message\n");
							break;
					}
					break;

				default:
					break;
			}
		}
	}

	if (!can_move()) {
		refresh_board(1);
		put_cur(1, 5);
		WRITES("\033[30;34m\033[22;41m          ");
		put_cur(1, 6);
		WRITES("YOU LOOSE!");
		put_cur(1, 7);
		WRITES("          \033[m\x0f");

		usleep(2000000);
	} else if ('b' == game.mode ||
			('2' == game.mode && MSG_LOST == MSG_CODE(msg))) {
		refresh_board(1);
		put_cur(1, 5);
		WRITES("\033[30;34m\033[22;41m          ");
		put_cur(1, 6);
		WRITES(" YOU WON !");
		put_cur(1, 7);
		WRITES("          \033[m\x0f");

		usleep(2000000);
	} else if ('2' == game.mode && MSG_QUIT == MSG_CODE(msg)) {
		refresh_board(1);
		put_cur(1, 5);
		WRITES("\033[30;34m\033[22;41m          ");
		put_cur(1, 6);
		WRITES("PEER LEFT ");
		put_cur(1, 7);
		WRITES("          \033[m\x0f");

		usleep(2000000);
	}

	/* flush non processed characters */
	while (1 == read(0, &key, 1));

	ret = tcsetattr(0, TCSANOW, &old_tios);
	if (-1 == ret) {
		perror("tcsetattr");
		return 1;
	}

	cleanup();
	put_cur(0, 0);
	if ('2' == game.mode) {
		close(net.sfd);
		close(net.fd);
	}

	return 0;
out:
	return 1;
}

/* SPDX-License-Identifier: GPL-2.0 */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "cmaze.h"

struct Cell {
	int row;
	int col;
	int value;
	int heuristic;
	bool is_path;
	CellColor color;

	struct Cell *parent;
};

struct Maze {
	int num_rows;
	int num_cols;
	struct Cell *board;

	bool difficult;
	uint anim_speed;

	bool solver_running;
	bool solver_cancel;
	GThread *solver_thread;
	SolverAlgorithm solver_algorithm;
	MazeSolverFunc solver_cb;
	void *solver_cb_userdata;

	int path_len;
	struct timeval solve_time;

	struct Cell *start_cell;
	struct Cell *end_cell;
};

static struct Cell *maze_get_cell(struct Maze *maze, int row, int col);

static int cell_is_wall(struct Maze *maze, int row, int col)
{
	struct Cell *cell;

	cell = maze_get_cell(maze, row, col);
	if (cell)
		return (cell->value == 0);

	return 1;
}

static int cell_cmp(struct Cell *c1, struct Cell *c2)
{
	if (c1->row < c2->row)
		return -1;
	if (c1->row > c2->row)
		return 1;

	if (c1->col < c2->col)
		return -1;
	if (c1->col > c2->col)
		return 1;

	return 0;
}

static struct Cell *cell_new(int row, int col)
{
	struct Cell *cell;

	cell = g_malloc0(sizeof(*cell));
	cell->row = row;
	cell->col = col;

	return cell;
}

static int cell_distance(struct Cell *cell1, struct Cell *cell2)
{
	return (abs(cell1->row - cell2->row) +
		abs(cell1->col - cell2->col));
}

static int cell_cmp_heuristic(const struct Cell *c1, const struct Cell *c2)
{
	if (c1->heuristic <= c2->heuristic)
		return -1;

	return 1;
}

static gint cell_cmp_lower_value(struct Cell *c1, struct Cell *c2)
{
	if (!cell_cmp(c1, c2) && c1->value < c2->value)
		return 0;

	return 1;
}

static struct Cell *maze_get_cell(struct Maze *maze, int row, int col)
{
	if (row < 0 || row >= maze->num_rows ||
	    col < 0 || col >= maze->num_cols)
		return NULL;

	return &maze->board[row * maze->num_cols + col];
}

bool maze_solver_running(struct Maze *maze)
{
	return maze->solver_running;
}

void maze_set_anim_speed(struct Maze *maze, uint speed)
{
	maze->anim_speed = (speed < 100) ? speed : 100;
}

uint maze_get_anim_speed(struct Maze *maze)
{
	return maze->anim_speed;
}

int maze_get_num_rows(struct Maze *maze)
{
	return maze->num_rows;
}

int maze_get_num_cols(struct Maze *maze)
{
	return maze->num_cols;
}

bool maze_get_difficult(struct Maze *maze)
{
	return maze->difficult;
}

int maze_get_path_length(struct Maze *maze)
{
	return maze->path_len;
}

double maze_get_solve_time(struct Maze *maze)
{
	return maze->solve_time.tv_sec + (0.000001f * maze->solve_time.tv_usec);
}

SolverAlgorithm maze_get_solver_algorithm(struct Maze *maze)
{
	return maze->solver_algorithm;
}

void maze_set_solver_algorithm(struct Maze *maze, SolverAlgorithm algo)
{
	maze->solver_algorithm = algo;
}

static void maze_clear_board(struct Maze *maze)
{
	struct Cell *cell;
	int i;

	for (i = 0; i < maze->num_rows * maze->num_cols; i++) {
		cell = &maze->board[i];

		if (cell->value == 0)
			continue;

		cell->value = 1;
		cell->is_path = false;
		cell->heuristic = 0;
		cell->color = WHITE;
	}
}

int maze_solve_a_star(struct Maze *maze)
{
	int neighbours[4][2] = { { -1, 0 },  { 0, 1 }, { 1, 0 }, { 0, -1 } };
	struct Cell *cell;
	GList *open = NULL;
	GList *closed = NULL;
	GList *elem;
	int i;
	int err = 0;
	struct Cell *board_cell;

	cell = cell_new(maze->start_cell->row, maze->start_cell->col);
	cell->value = 0;
	cell->heuristic = cell_distance(cell, maze->end_cell);

	open = g_list_append(open, cell);

	while (open != NULL) {
		if (maze->solver_cancel) {
			err = -EINTR;
			goto exit;
		}

		if (maze->anim_speed < 100)
			usleep(125 * (100 - maze->anim_speed));

		elem = g_list_first(open);
		cell = (struct Cell *)elem->data;
		open = g_list_delete_link(open, elem);

		closed = g_list_prepend(closed, cell);

		board_cell = maze_get_cell(maze, cell->row, cell->col);
		board_cell->color = LIGHTGRAY;

		if (!cell_cmp(cell, maze->end_cell)) {
			struct Cell *path;

			maze->path_len = 1;

			while (cell) {
				path = maze_get_cell(maze, cell->row, cell->col);
				path->is_path = true;
				path->color = GREEN;
				maze->path_len++;

				cell = cell->parent;
			}

			goto exit;
		}

		for (i = 0; i < 4; i++) {
			int *n = neighbours[i];
			int n_row = cell->row + n[0];
			int n_col = cell->col + n[1];
			struct Cell *n_cell;

			if (n_row < 0 || n_row >= maze->num_rows ||
			    n_col < 0 || n_col >= maze->num_cols)
				continue;

			if (cell_is_wall(maze, n_row, n_col))
				continue;

			n_cell = cell_new(n_row, n_col);

			if (g_list_find_custom(closed, n_cell,
					       (GCompareFunc)cell_cmp)) {
				g_free(n_cell);
				continue;
			}

			n_cell->parent = cell;
			n_cell->value = cell->value + 1;
			n_cell->heuristic = n_cell->value +
					  cell_distance(n_cell, maze->end_cell);

			// Lookup in open for same cell with a lower value
			if (!g_list_find_custom(open, n_cell,
					  (GCompareFunc)cell_cmp_lower_value)) {
				open = g_list_insert_sorted(open, n_cell,
					      (GCompareFunc)cell_cmp_heuristic);

				board_cell = maze_get_cell(maze, n_row, n_col);
				board_cell->color = DARKGRAY;
			} else {
				g_free(n_cell);
			}
		}
	}

	printf("No path found!\n");

exit:
	g_list_free_full(open, (GDestroyNotify)g_free);
	g_list_free_full(closed, (GDestroyNotify)g_free);

	return err;
}

#define ORIENTATION_NORTH 0
#define ORIENTATION_EAST  1
#define ORIENTATION_SOUTH 2
#define ORIENTATION_WEST  3

#define HEAD_QUEUE_LENGTH 50

int maze_solve_always_turn(struct Maze *maze)
{
	int left_neighbours[4][2] = { { 0, -1 }, { -1, 0 }, { 0, 1 }, { 1, 0 } };
	int right_neighbours[4][2] = { { 0, 1 },  { -1, 0 }, { 0, -1 }, { 1, 0 } };
	int *neighbours;
	struct Cell *cell;
	struct Cell *n_cell;
	int i;
	int orientation;
	int row;
	int col;
	int err = 0;
	int value;
	int low_value;
	GQueue *head_cells;

	neighbours = (maze->solver_algorithm == SOLVER_ALWAYS_TURN_LEFT) ?
			(int *)left_neighbours :
			(int *)right_neighbours;

	head_cells = g_queue_new();

	cell = maze->start_cell;
	orientation = ORIENTATION_EAST;

	value = 2;

	while (cell != maze->end_cell) {
		if (maze->solver_cancel) {
			err = -EINTR;
			goto exit;
		}

		if (maze->anim_speed < 100)
			usleep(125 * (100 - maze->anim_speed));

		cell->color = DARKGRAY;
		cell->value = value++;

		g_queue_push_tail(head_cells, cell);
		if (g_queue_get_length(head_cells) > HEAD_QUEUE_LENGTH) {
			n_cell = g_queue_pop_head(head_cells);
			if (g_queue_find(head_cells, n_cell) == NULL)
				n_cell->color = LIGHTGRAY;
		}

		row = cell->row;
		col = cell->col;

		for (i = 0; i < 4; i++) {
			int *n = neighbours + (((orientation + i) & 0x3) * 2);
			int n_row = row + n[0];
			int n_col = col + n[1];

			cell = maze_get_cell(maze, n_row, n_col);
			if (cell && cell->value > 0) {
				/* Not a wall. Go on */
				orientation = (orientation + i - 1) & 0x3;
				break;
			}
		}
	}

	/* Last cell */
	cell->value = value++;
	maze->path_len = 1;

	/* Reset color for the head cells */
	while ((n_cell = g_queue_pop_head(head_cells)) != NULL)
		n_cell->color = LIGHTGRAY;

	// Light up the shortest path
	while (cell != maze->start_cell) {
		if (maze->solver_cancel) {
			err = -EINTR;
			goto exit;
		}

		maze->path_len++;

		cell->color = GREEN;

		row = cell->row;
		col = cell->col;

		low_value = cell->value;

		for (i = 0; i < 4; i++) {
			int *n = right_neighbours[i];
			int n_row = row + n[0];
			int n_col = col + n[1];

			n_cell = maze_get_cell(maze, n_row, n_col);
			if (!n_cell)
				continue;

			if (n_cell->value > 1 && n_cell->value < low_value) {
				low_value = n_cell->value;
				cell = n_cell;
			}
		}
	}

	/* First cell */
	cell->color = GREEN;
	maze->path_len++;

exit:
	g_queue_free(head_cells);

	return err;
}

static void maze_solve_thread_join(struct Maze *maze)
{
	if (!maze->solver_thread)
		return;

	g_thread_join(maze->solver_thread);
	maze->solver_thread = NULL;
}

static gboolean maze_solve_monitor(struct Maze *maze)
{
	int reason;

	if (maze->solver_cancel)
		reason = SOLVER_CB_REASON_CANCELED;
	else if (maze->solver_running)
		reason = SOLVER_CB_REASON_RUNNING;
	else
		reason = SOLVER_CB_REASON_SOLVED;

	if (maze->solver_cb)
		maze->solver_cb(reason, maze->solver_cb_userdata);

	if (reason == SOLVER_CB_REASON_SOLVED)
		maze_solve_thread_join(maze);

	return maze->solver_running;
}

void maze_solve_thread_cancel(struct Maze *maze)
{
	maze->solver_cancel = true;

	maze_solve_thread_join(maze);
}

int maze_solve(struct Maze *maze)
{
	struct timeval start, end;
	int (*solver_func)(struct Maze *);
	int result;

	switch (maze->solver_algorithm) {
	case SOLVER_A_STAR:
		solver_func = maze_solve_a_star;
		break;
	case SOLVER_ALWAYS_TURN_LEFT:
	case SOLVER_ALWAYS_TURN_RIGHT:
		solver_func = maze_solve_always_turn;
		break;
	default:
		fprintf(stderr, "Invalid solver enum %d\n",
			maze->solver_algorithm);
		return -EINVAL;
	}

	maze_clear_board(maze);

	gettimeofday(&start, NULL);

	result = solver_func(maze);

	gettimeofday(&end, NULL);
	timersub(&end, &start, &maze->solve_time);

	maze->solver_running = false;

	return result;
}

int maze_solve_thread(struct Maze *maze, MazeSolverFunc cb, void *userdata)
{
	maze->solver_cancel = false;
	maze->solver_running = true;
	maze->solver_cb = cb;
	maze->solver_cb_userdata = userdata;

	maze->solver_thread = g_thread_new("solver",
			      (GThreadFunc)maze_solve, maze);

	g_timeout_add(40, (GSourceFunc)maze_solve_monitor, maze);

	return 0;
}

CellColor maze_get_cell_color(struct Maze *maze, int row, int col)
{
	struct Cell *cell;

	cell = maze_get_cell(maze, row, col);

	if (cell)
		return cell->color;

	return BLACK;
}

void maze_print_board(struct Maze *maze)
{
	int row;
	int col;
	struct Cell *cell;

	for (row = 0; row < maze->num_rows; row++) {
		for (col = 0; col < maze->num_cols; col++) {
			cell = &maze->board[row * maze->num_cols + col];

			if (cell->is_path)
				printf("O");
			else
				printf("%c", (cell->value) ? ' ' : 'X');
		}

		printf("\n");
	}
}

int maze_create(struct Maze *maze, int num_rows, int num_cols, bool difficult)
{
	int row;
	int col;
	int r;
	int i;
	struct Cell *cell;
	GList *stack = NULL;
	GList *elem;
	int neighbours[4][2] = { { -2, 0 },  { 0, 2 }, { 2, 0 }, { 0, -2 } };
	int walls[4][2] = { { -1, 0 },  { 0, 1 }, { 1, 0 }, { 0, -1 } };

	if (maze->solver_running)
		return -EINPROGRESS;

	if (num_rows < MAZE_MIN_ROWS)
		num_rows = MAZE_MIN_ROWS;
	else if (num_rows > MAZE_MAX_ROWS)
		num_rows = MAZE_MAX_ROWS;
	else if ((num_rows & 1) == 0)
		num_rows++;

	if (num_cols < MAZE_MIN_COLS)
		num_cols = MAZE_MIN_COLS;
	else if (num_cols > MAZE_MAX_COLS)
		num_cols = MAZE_MAX_COLS;
	else if ((num_cols & 1) == 0)
		num_cols++;

	if (maze->board &&
	    maze->num_rows * maze->num_cols < num_rows * num_cols) {
		g_free(maze->board);
		maze->board = NULL;
	}

	maze->num_rows = num_rows;
	maze->num_cols = num_cols;
	maze->difficult = difficult;

	if (!maze->board)
		maze->board = g_malloc(num_rows * num_cols * sizeof(struct Cell));

	memset(maze->board, 0, num_rows * num_cols * sizeof(struct Cell));

	for (row = 0; row < maze->num_rows; row++) {
		for (col = 0; col < maze->num_cols; col++) {
			cell = maze_get_cell(maze, row, col);

			cell->row = row;
			cell->col = col;
			if ((row & 1) && (col & 1)) {
				cell->value = 1;
				cell->color = WHITE;
			}
		}
	}

	row = (random() % (maze->num_rows - 2)) / 2 * 2 + 1;
	col = (random() % (maze->num_cols - 2)) / 2 * 2 + 1;
	cell = maze_get_cell(maze, row, col);
	if (!cell || cell_is_wall(maze, row, col))
		return -EINVAL;

	cell->value = 2;
	stack = g_list_prepend(stack, cell);

	while (stack != NULL) {
		elem = g_list_first(stack);
		cell = elem->data;
		stack = g_list_delete_link(stack, elem);

		row = cell->row;
		col = cell->col;

		r = random() % 4;
		for (i = 0; i < 4; i++) {
			int *n = neighbours[(i + r) % 4];
			int n_row = row + n[0];
			int n_col = col + n[1];
			struct Cell *n_cell;
			struct Cell *w_cell;
			int *w;

			if (n_row < 0 || n_row >= maze->num_rows ||
			    n_col < 0 || n_col >= maze->num_cols)
				continue;

			n_cell = maze_get_cell(maze, n_row, n_col);
			if (!n_cell)
				return -EINVAL;

			if (n_cell->value == 2)
				continue;

			stack = g_list_prepend(stack, cell);

			n_cell->value = 2;
			stack = g_list_prepend(stack, n_cell);

			// Remove wall between cells
			w = walls[(i + r) % 4];
			w_cell = maze_get_cell(maze, row + w[0], col + w[1]);
			w_cell->value = 2;
			w_cell->color = WHITE;

			break;
		}

		cell->value = 2;
	}

	maze->start_cell = maze_get_cell(maze, 1, 0);
	maze->start_cell->value = 2;
	maze->start_cell->color = RED;
	maze->end_cell = maze_get_cell(maze, maze->num_rows - 2, maze->num_cols - 1);
	maze->end_cell->value = 2;
	maze->end_cell->color = RED;

	if (!difficult)
		return 0;

	for (i = 0; i < MAX(maze->num_rows, maze->num_cols); i++) {
		while (1) {
			row = (random() % (maze->num_rows - 2)) + 1;
			col = (random() % (maze->num_cols - 2)) + 1;
			cell = maze_get_cell(maze, row, col);

			if (! cell_is_wall(maze, row, col))
				continue;

			r = 0;
			if (cell_is_wall(maze, row - 1, col))
				r += 1;
			if (cell_is_wall(maze, row + 1, col))
				r += 1;
			// 1 wall up or down means we're on a
			// wall end or at the top of a T. We need
			// to choose another wall
			if (r == 1)
				continue;

			if (cell_is_wall(maze, row, col - 1))
				r += 1;
			if (cell_is_wall(maze, row, col + 1))
				r += 1;

			// We're surounded by 2 walls verticaly
			// or horizontaly. It's a match
			if (r == 2)
				break;
		}

		cell->value = 2;
		cell->color = WHITE;
	}

	return 0;
}

struct Maze *maze_alloc(void)
{
	struct Maze *maze;

	maze = g_malloc0(sizeof(*maze));

	return maze;
}

void maze_free(struct Maze *maze)
{
	if (!maze)
		return;

	if (maze->board)
		g_free(maze->board);

	g_free(maze);
}

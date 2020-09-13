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

	struct list_head node;
	struct Cell *parent;
};

struct Maze {
	int num_rows;
	int num_cols;
	struct Cell *board;

	bool difficult;
	bool animate;

	bool solver_running;
	bool solver_cancel;
	GThread *solver_thread;
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

	return 0;
}

static int cell_equals(struct Cell *c1, struct Cell *c2)
{
	return (c1->row == c2->row && c1->col == c2->col);
}

static struct Cell *cell_new(int row, int col)
{
	struct Cell *cell;

	cell = calloc(1, sizeof(*cell));
	if (cell) {
		cell->row = row;
		cell->col = col;
		//cell->node = LIST_HEAD_INIT(node);
	}

	return cell;
}

static int cell_distance(struct Cell *cell1, struct Cell *cell2)
{
	return (abs(cell1->row - cell2->row) +
		abs(cell1->col - cell2->col));
}

static bool cell_list_lookup_lower_value(struct Cell *cell,
					 struct list_head *list)
{
	struct Cell *c;

	list_for_each_entry(c, list, node) {
		if (cell_equals(c, cell) && c->value < cell->value)
			return true;
	}

	return false;
}

static bool cell_list_lookup(int row, int col, struct list_head *list)
{
	struct Cell *c;
	struct Cell cell;

	cell.row = row;
	cell.col = col;

	list_for_each_entry(c, list, node) {
		if (cell_equals(c, &cell))
			return true;
	}

	return false;
}

static struct Cell *maze_get_cell(struct Maze *maze, int row, int col)
{
	if (row < 0 || row >= maze->num_rows ||
	    col < 0 || col >= maze->num_cols) {
		printf("Cannot get cell(%i, %i)\n", row, col);
		return NULL;
	}

	return &maze->board[row * maze->num_cols + col];
}

bool maze_solver_running(struct Maze *maze)
{
	return maze->solver_running;
}

void maze_set_animate(struct Maze *maze, bool animate)
{
	maze->animate = animate;
}

bool maze_get_animate(struct Maze *maze)
{
	return maze->animate;
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

int maze_solve(struct Maze *maze)
{
	int neighbours[4][2] = { { -1, 0 },  { 0, 1 }, { 1, 0 }, { 0, -1 } };
	struct Cell *cell;
	struct Cell *c;
	LIST_HEAD(open);
	LIST_HEAD(closed);
	int i;
	struct timeval start, end;
	struct Cell *board_cell;

	gettimeofday(&start, NULL);

	maze_clear_board(maze);

	cell = cell_new(maze->start_cell->row, maze->start_cell->col);
	cell->value = 0;
	cell->heuristic = cell_distance(cell, maze->end_cell);

	list_add_tail(&cell->node, &open);

	while (! list_empty(&open)) {
		if (maze->solver_cancel)
			goto exit;

		if (maze->animate)
			usleep(100);

		cell = list_last_entry(&open, struct Cell, node);
		list_del(&cell->node);
		list_add(&cell->node, &closed);

		board_cell = maze_get_cell(maze, cell->row, cell->col);
		board_cell->color = LIGHTGRAY;

		if (cell_equals(cell, maze->end_cell)) {
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

			if (cell_list_lookup(n_row, n_col, &closed))
				continue;

			n_cell = cell_new(n_row, n_col);
			n_cell->parent = cell;
			n_cell->value = cell->value + 1;
			n_cell->heuristic = n_cell->value +
					  cell_distance(n_cell, maze->end_cell);

			// Lookup in open for same cell with a lower value
			if (! cell_list_lookup_lower_value(n_cell, &open)) {
				list_add(&n_cell->node, &open);
				board_cell = maze_get_cell(maze, n_row, n_col);
				board_cell->color = DARKGRAY;
			} else {
				free(n_cell);
			}
		}
	}

	printf("No path found!\n");

exit:
	gettimeofday(&end, NULL);
	timersub(&end, &start, &maze->solve_time);

	list_for_each_entry_safe(cell, c, &open, node) {
		list_del(&cell->node);
		free(cell);
	}

	list_for_each_entry_safe(cell, c, &closed, node) {
		list_del(&cell->node);
		free(cell);
	}

	maze->solver_running = false;

	return 0;
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

int maze_solve_thread(struct Maze *maze, MazeSolverFunc cb, void *userdata)
{
	maze->solver_cancel = false;
	maze->solver_running = true;
	maze->solver_cb = cb;
	maze->solver_cb_userdata = userdata;

	maze->solver_thread = g_thread_new("solver",
			      (GThreadFunc)maze_solve, maze);

	g_timeout_add(100, (GSourceFunc)maze_solve_monitor, maze);

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
	LIST_HEAD(stack);
	int neighbours[4][2] = { { -2, 0 },  { 0, 2 }, { 2, 0 }, { 0, -2 } };
	int walls[4][2] = { { -1, 0 },  { 0, 1 }, { 1, 0 }, { 0, -1 } };

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
		free(maze->board);
		maze->board = NULL;
	}

	maze->num_rows = num_rows;
	maze->num_cols = num_cols;
	maze->difficult = difficult;

	if (!maze->board) {
		maze->board = malloc(num_rows * num_cols * sizeof(struct Cell));
		if (!maze->board) {
			fprintf(stderr, "Failed to allocate memory\n");
			return -ENOMEM;
		}
	}

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
	list_add_tail(&cell->node, &stack);

	while (! list_empty(&stack)) {
		cell = list_last_entry(&stack, struct Cell, node);
		list_del_init(&cell->node);
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

			list_add_tail(&cell->node, &stack);

			n_cell->value = 2;
			list_add_tail(&n_cell->node, &stack);

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
	}

	return 0;
}

struct Maze *maze_alloc(void)
{
	struct Maze *maze;

	maze = calloc(1, sizeof(*maze));

	return maze;
}

void maze_free(struct Maze *maze)
{
	if (!maze)
		return;

	if (maze->board)
		free(maze->board);

	free(maze);
}

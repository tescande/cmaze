/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __MAZE_H__
#define __MAZE_H__

#include <stdbool.h>

#include <glib.h>

#include "list.h"

#define MAZE_MIN_ROWS 21
#define MAZE_MIN_COLS 21
#define MAZE_MAX_ROWS 499
#define MAZE_MAX_COLS 499

#define SOLVER_CB_REASON_RUNNING  0
#define SOLVER_CB_REASON_SOLVED   1
#define SOLVER_CB_REASON_CANCELED 2

typedef void(*MazeSolverFunc)(int, void *);

typedef enum {
	BLACK = 0,
	WHITE,
	RED,
	GREEN,
	LIGHTGRAY,
	DARKGRAY,
} CellColor;

struct Cell;

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

struct Maze *maze_alloc(void);
void maze_free(struct Maze *maze);

int maze_create(struct Maze *maze, int num_rows, int num_cols, bool difficult);
int maze_solve(struct Maze *maze);
void maze_print_board(struct Maze *maze);

int maze_solve_thread(struct Maze *maze, MazeSolverFunc cb, void *userdata);
void maze_solve_thread_cancel(struct Maze *maze);

CellColor maze_get_cell_color(struct Maze *maze, int row, int col);

#endif /* __MAZE_H__ */

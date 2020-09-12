/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __MAZE_H__
#define __MAZE_H__

#include <stdbool.h>

#include "list.h"

#define MAZE_MIN_ROWS 21
#define MAZE_MIN_COLS 21
#define MAZE_MAX_ROWS 499
#define MAZE_MAX_COLS 499

struct Cell {
	int row;
	int col;
	int value;
	int heuristic;
	bool is_path;

	struct list_head node;
	struct Cell *parent;
};

struct Maze {
	int num_rows;
	int num_cols;
	struct Cell *board;

	struct Cell *start_cell;
	struct Cell *end_cell;
};

struct Maze *maze_alloc(void);
void maze_free(struct Maze *maze);

int maze_create(struct Maze *maze, int num_rows, int num_cols);
int maze_solve(struct Maze *maze);
void maze_print_board(struct Maze *maze);

#endif /* __MAZE_H__ */

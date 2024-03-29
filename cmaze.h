/* SPDX-License-Identifier: MIT */
#ifndef __MAZE_H__
#define __MAZE_H__

#include <glib.h>
#include <glib/gprintf.h>

#define MAZE_MIN_ROWS 21
#define MAZE_MIN_COLS 21
#define MAZE_MAX_ROWS 499
#define MAZE_MAX_COLS 499

#define SOLVER_CB_REASON_RUNNING  0
#define SOLVER_CB_REASON_SOLVED   1
#define SOLVER_CB_REASON_CANCELED 2
#define SOLVER_CB_REASON_INFLOOP  3

typedef void(*MazeSolverFunc)(int, void *);

typedef enum {
	SOLVER_BFS = 0,
	SOLVER_DFS,
	SOLVER_A_STAR,
	SOLVER_ALWAYS_TURN_LEFT,
	SOLVER_ALWAYS_TURN_RIGHT,
} SolverAlgorithm;

typedef enum {
	CELL_TYPE_EMPTY = 0,
	CELL_TYPE_WALL,
	CELL_TYPE_END,
	CELL_TYPE_START,
	CELL_TYPE_PATH_HEAD,
	CELL_TYPE_PATH_VISITED,
	CELL_TYPE_PATH_SOLUTION,
} CellType;

struct Cell;
struct Maze;

struct Maze *maze_alloc(void);
void maze_free(struct Maze *maze);

int maze_create(struct Maze *maze, int num_rows, int num_cols, gboolean complex);
int maze_solve(struct Maze *maze);
void maze_print_board(struct Maze *maze);

int maze_solve_thread(struct Maze *maze, MazeSolverFunc cb, void *userdata);
void maze_solve_thread_cancel(struct Maze *maze);

gboolean maze_solver_running(struct Maze *maze);
int maze_get_path_length(struct Maze *maze);
float maze_get_solve_time(struct Maze *maze);

void maze_clear_board(struct Maze *maze);

void maze_set_anim_speed(struct Maze *maze, uint speed);
uint maze_get_anim_speed(struct Maze *maze);

int maze_get_num_rows(struct Maze *maze);
int maze_get_num_cols(struct Maze *maze);

int maze_set_start_cell(struct Maze *maze, int row, int col);
int maze_set_end_cell(struct Maze *maze, int row, int col);

gboolean maze_get_difficult(struct Maze *maze);

SolverAlgorithm maze_get_solver_algorithm(struct Maze *maze);
void maze_set_solver_algorithm(struct Maze *maze, SolverAlgorithm algo);

CellType maze_get_cell_type(struct Maze *maze, int row, int col);

int gtk_maze_run(struct Maze *maze);

#endif /* __MAZE_H__ */

/* SPDX-License-Identifier: MIT */
#include "cmaze.h"

struct Cell {
	int row;
	int col;
	int value;
	int heuristic;
	CellType type;

	struct Cell *parent;
};

struct Maze {
	int num_rows;
	int num_cols;
	struct Cell *board;

	gboolean difficult;
	uint anim_speed;

	gboolean solver_running;
	gboolean solver_cancel;
	GThread *solver_thread;
	SolverAlgorithm solver_algorithm;
	MazeSolverFunc solver_cb;
	void *solver_cb_userdata;

	int path_len;
	gint64 solve_time;

	struct Cell *start_cell;
	struct Cell *end_cell;
};

static struct Cell *maze_get_cell(struct Maze *maze, int row, int col);

static int cell_is_wall(struct Maze *maze, int row, int col)
{
	struct Cell *cell;

	cell = maze_get_cell(maze, row, col);
	if (cell)
		return (cell->type == CELL_TYPE_WALL);

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

static gboolean maze_cell_is_perimeter(struct Maze *maze, struct Cell *cell)
{
	return cell->row == 0 || cell->col == 0 ||
	       cell->row >= maze->num_rows - 1 ||
	       cell->col >= maze->num_cols - 1;
}

static void maze_cell_reset(struct Maze *maze, struct Cell *cell)
{
	if (!cell)
		return;

	if (maze_cell_is_perimeter(maze, cell))
		cell->type = CELL_TYPE_WALL;
	else if (cell->type != CELL_TYPE_WALL)
		cell->type = CELL_TYPE_EMPTY;
}

static struct Cell *maze_get_cell_for_start_or_end(struct Maze *maze, int row, int col)
{
	int neighbours[4][2] = { { -1, 0 },  { 0, 1 }, { 1, 0 }, { 0, -1 } };
	struct Cell *cell;
	struct Cell *n_cell;
	int i;

	if (maze->solver_running)
		return NULL;

	cell = maze_get_cell(maze, row, col);
	if (!cell)
		return NULL;

	if (cell->type == CELL_TYPE_WALL) {
		if (!maze_cell_is_perimeter(maze, cell))
			return NULL;

		/* The cell is part of the perimeter walls */
		for (i = 0; i < 4; i++) {
			int *n = neighbours[i];
			int n_row = cell->row + n[0];
			int n_col = cell->col + n[1];

			n_cell = maze_get_cell(maze, n_row, n_col);
			if (!n_cell || n_cell->type == CELL_TYPE_WALL)
				continue;

			return cell;
		}

		return NULL;
	}

	return cell;
}

int maze_set_end_cell(struct Maze *maze, int row, int col)
{
	struct Cell *cell;

	cell = maze_get_cell_for_start_or_end(maze, row, col);
	if (!cell)
		return -1;

	/* Reset previous start_cell */
	maze_cell_reset(maze, maze->end_cell);

	cell->type = CELL_TYPE_END;
	maze->end_cell = cell;

	return 0;
}

int maze_set_start_cell(struct Maze *maze, int row, int col)
{
	struct Cell *cell;

	cell = maze_get_cell_for_start_or_end(maze, row, col);
	if (!cell)
		return -1;

	/* Reset previous start_cell */
	maze_cell_reset(maze, maze->start_cell);

	cell->type = CELL_TYPE_START;
	maze->start_cell = cell;

	return 0;
}

gboolean maze_solver_running(struct Maze *maze)
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

gboolean maze_get_difficult(struct Maze *maze)
{
	return maze->difficult;
}

int maze_get_path_length(struct Maze *maze)
{
	return maze->path_len;
}

float maze_get_solve_time(struct Maze *maze)
{
	return (float)maze->solve_time / G_USEC_PER_SEC;
}

SolverAlgorithm maze_get_solver_algorithm(struct Maze *maze)
{
	return maze->solver_algorithm;
}

void maze_set_solver_algorithm(struct Maze *maze, SolverAlgorithm algo)
{
	maze->solver_algorithm = algo;
}

static void _maze_clear_board(struct Maze *maze)
{
	struct Cell *cell;
	int i;

	for (i = 0; i < maze->num_rows * maze->num_cols; i++) {
		cell = &maze->board[i];

		cell->value = 0;
		cell->heuristic = 0;
		cell->parent = NULL;
		if (cell->type != CELL_TYPE_WALL)
			cell->type = CELL_TYPE_EMPTY;
	}

	maze->start_cell->type = CELL_TYPE_START;
	maze->end_cell->type = CELL_TYPE_END;
}

void maze_clear_board(struct Maze *maze)
{
	if (maze->solver_running)
		return;

	_maze_clear_board(maze);
}

static int maze_solve_a_star(struct Maze *maze)
{
	int neighbours[4][2] = { { -1, 0 },  { 0, 1 }, { 1, 0 }, { 0, -1 } };
	struct Cell *cell;
	struct Cell *path;
	GList *open = NULL;
	GList *closed = NULL;
	GList *elem;
	int i;
	int err = 0;
	struct Cell *board_cell;

	cell = cell_new(maze->start_cell->row, maze->start_cell->col);
	cell->value = 1;
	cell->heuristic = cell_distance(cell, maze->end_cell);

	open = g_list_append(open, cell);

	while (open != NULL) {
		if (maze->solver_cancel) {
			err = -1;
			goto exit;
		}

		if (maze->anim_speed < 100)
			g_usleep(125 * (100 - maze->anim_speed));

		elem = g_list_first(open);
		cell = (struct Cell *)elem->data;
		open = g_list_delete_link(open, elem);

		closed = g_list_prepend(closed, cell);

		board_cell = maze_get_cell(maze, cell->row, cell->col);
		board_cell->type = CELL_TYPE_PATH_VISITED;

		if (!cell_cmp(cell, maze->end_cell))
			break;

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

			/* Lookup in open for same cell with a lower value */
			if (!g_list_find_custom(open, n_cell,
					  (GCompareFunc)cell_cmp_lower_value)) {
				open = g_list_insert_sorted(open, n_cell,
					      (GCompareFunc)cell_cmp_heuristic);

				board_cell = maze_get_cell(maze, n_row, n_col);
				board_cell->type = CELL_TYPE_PATH_HEAD;
			} else {
				g_free(n_cell);
			}
		}
	}

	maze->path_len = 0;

	while (cell) {
		path = maze_get_cell(maze, cell->row, cell->col);
		path->type = CELL_TYPE_PATH_SOLUTION;
		maze->path_len++;

		cell = cell->parent;
	}

	maze->start_cell->type = CELL_TYPE_START;
	maze->end_cell->type = CELL_TYPE_END;

exit:
	g_list_free_full(open, (GDestroyNotify)g_free);
	g_list_free_full(closed, (GDestroyNotify)g_free);

	return err;
}

static void maze_set_solution_path(struct Maze *maze)
{
	int neighbours[4][2] = { { 0, -1 }, { -1, 0 }, { 0, 1 }, { 1, 0 } };
	struct Cell *cell;
	struct Cell *n_cell;
	int low_value;
	int row;
	int col;
	int n_row;
	int n_col;
	int *n;
	int i;

	cell = maze->end_cell;
	maze->path_len = 1;

	/* Light up the shortest path */
	while (cell != maze->start_cell) {
		if (maze->solver_cancel)
			return;

		maze->path_len++;
		cell->type = CELL_TYPE_PATH_SOLUTION;
		low_value = cell->value;

		row = cell->row;
		col = cell->col;

		/* Search for a neighbours with the lowest value */
		for (i = 0; i < 4; i++) {
			n = neighbours[i];
			n_row = row + n[0];
			n_col = col + n[1];

			n_cell = maze_get_cell(maze, n_row, n_col);
			if (!n_cell || n_cell->type == CELL_TYPE_WALL)
				continue;

			if (n_cell->value && n_cell->value < low_value) {
				low_value = n_cell->value;
				cell = n_cell;
			}
		}
	}

	maze->start_cell->type = CELL_TYPE_START;
	maze->end_cell->type = CELL_TYPE_END;
}

#define ORIENTATION_NORTH 0
#define ORIENTATION_EAST  1
#define ORIENTATION_SOUTH 2
#define ORIENTATION_WEST  3

#define HEAD_QUEUE_LENGTH 50

static int maze_solve_always_turn(struct Maze *maze)
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
	GQueue *head_cells;

	neighbours = (maze->solver_algorithm == SOLVER_ALWAYS_TURN_LEFT) ?
			(int *)left_neighbours :
			(int *)right_neighbours;

	head_cells = g_queue_new();

	cell = maze->start_cell;
	orientation = ORIENTATION_EAST;

	value = 1;

	while (cell != maze->end_cell) {
		if (maze->solver_cancel) {
			err = -1;
			goto exit;
		}

		if (maze->anim_speed < 100)
			g_usleep(125 * (100 - maze->anim_speed));

		cell->type = CELL_TYPE_PATH_HEAD;
		cell->value = value++;

		g_queue_push_tail(head_cells, cell);
		if (g_queue_get_length(head_cells) > HEAD_QUEUE_LENGTH) {
			n_cell = g_queue_pop_head(head_cells);
			if (g_queue_find(head_cells, n_cell) == NULL)
				n_cell->type = CELL_TYPE_PATH_VISITED;
		}

		row = cell->row;
		col = cell->col;

		for (i = 0; i < 4; i++) {
			int *n = neighbours + (((orientation + i) & 0x3) * 2);
			int n_row = row + n[0];
			int n_col = col + n[1];

			cell = maze_get_cell(maze, n_row, n_col);
			if (cell && cell->type != CELL_TYPE_WALL) {
				/* Not a wall. Go on */
				orientation = (orientation + i - 1) & 0x3;
				break;
			}
		}
	}

	/* Last cell */
	cell->value = value++;

	/* Reset color for the head cells */
	while ((n_cell = g_queue_pop_head(head_cells)) != NULL)
		n_cell->type = CELL_TYPE_PATH_VISITED;

	maze_set_solution_path(maze);

exit:
	g_queue_free(head_cells);

	return err;
}

/**
 * procedure DFS_iterative(G, v) is
 *    let S be a stack
 *    S.push(v)
 *    while S is not empty do
 *        v = S.pop()
 *        if v is not labeled as discovered then
 *            label v as discovered
 *            for all edges from v to w in G.adjacentEdges(v) do
 *                S.push(w)
 */
static int maze_solve_dfs(struct Maze *maze)
{
	int neighbours[4][2] = { { -1, 0 },  { 0, 1 }, { 1, 0 }, { 0, -1 } };
	GList *stack = NULL;
	GList *elem;
	struct Cell *cell;
	struct Cell *n_cell;
	int n_row;
	int n_col;
	int *n;
	int i;
	int err = 0;

	stack = g_list_prepend(stack, maze->start_cell);

	while (stack != NULL) {
		if (maze->solver_cancel) {
			err = -1;
			goto exit;
		}

		if (maze->anim_speed < 100)
			g_usleep(125 * (100 - maze->anim_speed));

		elem = g_list_first(stack);
		cell = elem->data;
		stack = g_list_delete_link(stack, elem);

		if (cell->value)
			continue;

		cell->value = cell->parent ? cell->parent->value + 1 : 1;
		cell->type = CELL_TYPE_PATH_HEAD;

		if (cell == maze->end_cell)
			break;

		for (i = 0; i < 4; i++) {
			n = neighbours[i];
			n_row = cell->row + n[0];
			n_col = cell->col + n[1];

			n_cell = maze_get_cell(maze, n_row, n_col);

			if (!n_cell || n_cell->type == CELL_TYPE_WALL)
				continue;

			n_cell->parent = cell;
			n_cell->type = CELL_TYPE_PATH_VISITED;
			stack = g_list_prepend(stack, n_cell);
		}
	}

	maze_set_solution_path(maze);

exit:
	g_list_free(stack);

	return err;
}

/**
 * procedure BFS(G, root) is
 * let Q be a queue
 *     label root as discovered
 *     Q.enqueue(root)
 *     while Q is not empty do
 *         v := Q.dequeue()
 *         if v is the goal then
 *             return v
 *         for all edges from v to w in G.adjacentEdges(v) do
 *             if w is not labeled as discovered then
 *                 label w as discovered
 *                 Q.enqueue(w)
 */
int maze_solve_bfs(struct Maze *maze)
{
	int neighbours[4][2] = { { -1, 0 },  { 0, 1 }, { 1, 0 }, { 0, -1 } };
	GQueue *queue = NULL;
	struct Cell *cell;
	struct Cell *n_cell;
	int n_row;
	int n_col;
	int *n;
	int i;
	int err = 0;

	queue = g_queue_new();
	maze->start_cell->value = 1;
	g_queue_push_tail(queue, maze->start_cell);

	while (!g_queue_is_empty(queue)) {
		if (maze->solver_cancel) {
			err = -1;
			goto exit;
		}

		if (maze->anim_speed < 100)
			g_usleep(125 * (100 - maze->anim_speed));

		cell = g_queue_pop_head(queue);

		if (cell == maze->end_cell)
			break;

		cell->type = CELL_TYPE_PATH_VISITED;

		for (i = 0; i < 4; i++) {
			n = neighbours[i];
			n_row = cell->row + n[0];
			n_col = cell->col + n[1];

			n_cell = maze_get_cell(maze, n_row, n_col);

			if (!n_cell || n_cell->type == CELL_TYPE_WALL || n_cell->value)
				continue;

			n_cell->value = cell->value + 1;
			n_cell->type = CELL_TYPE_PATH_HEAD;
			g_queue_push_tail(queue, n_cell);
		}
	}

	maze_set_solution_path(maze);

exit:
	g_queue_free(queue);

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
	maze->solver_cancel = TRUE;

	maze_solve_thread_join(maze);
}

typedef int (*SolverFunc)(struct Maze *);

int maze_solve(struct Maze *maze)
{
	gint64 start;
	SolverFunc solver_func;
	int result;

	switch (maze->solver_algorithm) {
	case SOLVER_A_STAR:
		solver_func = maze_solve_a_star;
		break;
	case SOLVER_ALWAYS_TURN_LEFT:
	case SOLVER_ALWAYS_TURN_RIGHT:
		solver_func = maze_solve_always_turn;
		break;
	case SOLVER_DFS:
		solver_func = maze_solve_dfs;
		break;
	case SOLVER_BFS:
		solver_func = maze_solve_bfs;
		break;
	default:
		g_fprintf(stderr, "Invalid solver enum %d\n",
			  maze->solver_algorithm);
		return -1;
	}

	_maze_clear_board(maze);

	start = g_get_monotonic_time();

	result = solver_func(maze);

	maze->solve_time = g_get_monotonic_time() - start;

	maze->solver_running = FALSE;

	return result;
}

int maze_solve_thread(struct Maze *maze, MazeSolverFunc cb, void *userdata)
{
	maze->solver_cancel = FALSE;
	maze->solver_running = TRUE;
	maze->solver_cb = cb;
	maze->solver_cb_userdata = userdata;

	maze->solver_thread = g_thread_new("solver",
			      (GThreadFunc)maze_solve, maze);

	g_timeout_add(40, (GSourceFunc)maze_solve_monitor, maze);

	return 0;
}

CellType maze_get_cell_type(struct Maze *maze, int row, int col)
{
	struct Cell *cell;

	cell = maze_get_cell(maze, row, col);
	if (cell)
		return cell->type;

	return 0;
}

void maze_print_board(struct Maze *maze)
{
	int row;
	int col;
	struct Cell *cell;

	for (row = 0; row < maze->num_rows; row++) {
		for (col = 0; col < maze->num_cols; col++) {
			cell = &maze->board[row * maze->num_cols + col];

			if (cell->type == CELL_TYPE_PATH_SOLUTION)
				g_printf("O");
			else
				g_printf("%c", (cell->type == CELL_TYPE_WALL) ?
					       'X' : ' ');
		}

		g_printf("\n");
	}
}

int maze_create(struct Maze *maze, int num_rows, int num_cols, gboolean difficult)
{
	int neighbours[4][2] = { { -2, 0 },  { 0, 2 }, { 2, 0 }, { 0, -2 } };
	int walls[4][2] = { { -1, 0 },  { 0, 1 }, { 1, 0 }, { 0, -1 } };
	struct Cell *cell;
	struct Cell *n_cell;
	GList *stack = NULL;
	GList *elem;
	int row;
	int col;
	int n_row;
	int n_col;
	int r;
	int i;
	int *n;

	if (maze->solver_running)
		return -1;

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
			if ((row & 1) && (col & 1))
				cell->type = CELL_TYPE_EMPTY;
			else
				cell->type = CELL_TYPE_WALL;
		}
	}

	row = (random() % (maze->num_rows - 2)) / 2 * 2 + 1;
	col = (random() % (maze->num_cols - 2)) / 2 * 2 + 1;
	cell = maze_get_cell(maze, row, col);
	if (!cell || cell_is_wall(maze, row, col))
		return -1;

	cell->value = 1;
	stack = g_list_prepend(stack, cell);

	while (stack != NULL) {
		elem = g_list_first(stack);
		cell = elem->data;

		row = cell->row;
		col = cell->col;

		r = random() % 4;
		i = 0;
		while (i < 4) {
			n = neighbours[(i + r) % 4];
			n_row = row + n[0];
			n_col = col + n[1];

			n_cell = maze_get_cell(maze, n_row, n_col);
			if (!n_cell || n_cell->value == 1) {
				i++;
				continue;
			}

			n_cell->value = 1;
			stack = g_list_prepend(stack, n_cell);

			/* Remove wall between cells */
			n = walls[(i + r) % 4];
			n_cell = maze_get_cell(maze, row + n[0], col + n[1]);
			n_cell->value = 1;
			n_cell->type = CELL_TYPE_EMPTY;

			break;
		}

		/*
		 * No more suitable neighbour for this cell. We can remove it
		 * from the stack
		 */
		if (i >= 4)
			stack = g_list_delete_link(stack, elem);
	}

	maze->start_cell = maze_get_cell(maze, 1, 0);
	maze->start_cell->type = CELL_TYPE_START;
	maze->end_cell = maze_get_cell(maze, maze->num_rows - 2, maze->num_cols - 1);
	maze->end_cell->type = CELL_TYPE_END;

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
			/*
			 * Only 1 wall up or down means we're on a wall end or
			 * at the top of a T. Try with another wall.
			 */
			if (r == 1)
				continue;

			if (cell_is_wall(maze, row, col - 1))
				r += 1;
			if (cell_is_wall(maze, row, col + 1))
				r += 1;

			/*
			 * We're surounded by 2 walls verticaly or horizontaly.
			 * It's a match.
			 */
			if (r == 2)
				break;
		}

		/* Remove that wall */
		cell->type = CELL_TYPE_EMPTY;
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

	g_free(maze->board);

	g_free(maze);
}

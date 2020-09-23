/* SPDX-License-Identifier: GPL-2.0 */
#include <ctype.h>
#include <stdarg.h>

#include <gtk/gtk.h>

#include "cmaze.h"

struct MazeGui {
	struct Maze *maze;

	GtkWidget *drawing_area;
	GtkLabel  *info_label;
	GtkEntry  *entry_num_rows;
	GtkEntry  *entry_num_cols;
	GtkWidget *new_button;
	GtkWidget *solve_button;
	GtkToggleButton *difficult_check;
	GtkComboBoxText *algo_combo;

	int cell_width;
	int cell_height;
	cairo_surface_t *surface;
	cairo_t *cr;
};

static void get_gdk_color(CellColor cell_color, GdkRGBA *color)
{
	color->alpha = 1.0;

	switch (cell_color) {
	case BLACK:
		color->red =
		color->green =
		color->blue = 0;
		break;
	case WHITE:
		color->red =
		color->green =
		color->blue = 1.0;
		break;
	case RED:
		color->red = 1.0;
		color->green =
		color->blue = 0;
		break;
	case GREEN:
		color->green = 1.0;
		color->red =
		color->blue = 0;
		break;
	case LIGHTGRAY:
		color->green =
		color->red =
		color->blue = 0.8;
		break;
	case DARKGRAY:
		color->green =
		color->red =
		color->blue = 0.5;
		break;
	}
}

static void label_set_text(GtkLabel *label, char *format, ...)
{
	char *buf = NULL;
	int size;
	va_list ap;

	if (!format)
		goto exit_err;

	va_start(ap, format);
	size = vsnprintf(NULL, 0, format, ap);
	va_end(ap);
	if (size < 0)
		goto exit_err;

	size++;
	buf = g_malloc0(size);

	va_start(ap, format);
	size = vsnprintf(buf, size, format, ap);
	va_end(ap);
	if (size < 0)
		goto exit_err;

	gtk_label_set_text(label, buf);
	g_free(buf);

	return;

exit_err:
	g_free(buf);

	fprintf(stderr, "Can't set text label\n");
}

static int entry_get_number(GtkEntry *entry)
{
	const char *str;
	int num;

	str = gtk_entry_get_text(entry);
	if (sscanf(str, "%u", &num) != 1)
		num = -1;

	return num;
}

static void entry_set_number(GtkEntry *entry, int number)
{
	char buf[5] = { 0 };

	snprintf(buf, 4, "%d", number);
	gtk_entry_set_text(entry, buf);
}

static void cairo_surface_free(struct MazeGui *gui)
{
	cairo_destroy(gui->cr);
	cairo_surface_destroy(gui->surface);
}

static void cairo_surface_alloc(struct MazeGui *gui)
{
	GtkAllocation rect;
	int num_rows;
	int num_cols;
	int surface_width;
	int surface_height;

	num_rows = maze_get_num_rows(gui->maze);
	num_cols = maze_get_num_cols(gui->maze);

	gdk_monitor_get_workarea(
		gdk_display_get_primary_monitor(gdk_display_get_default()),
		&rect);

	gui->cell_width = (rect.width / num_cols) + 1;
	gui->cell_height = (rect.height / num_rows) + 1;

	surface_width = gui->cell_width * num_cols;
	surface_height = gui->cell_height * num_rows;

	gui->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
						  surface_width,
						  surface_height);
	gui->cr = cairo_create(gui->surface);
}

void on_new_clicked(GtkButton *button, struct MazeGui *gui)
{
	int num_rows;
	int num_cols;
	gboolean difficult;

	num_rows = entry_get_number(gui->entry_num_rows);
	num_cols = entry_get_number(gui->entry_num_cols);
	difficult = gtk_toggle_button_get_active(gui->difficult_check);

	maze_create(gui->maze, num_rows, num_cols, difficult);

	entry_set_number(gui->entry_num_rows, maze_get_num_rows(gui->maze));
	entry_set_number(gui->entry_num_cols, maze_get_num_cols(gui->maze));

	cairo_surface_free(gui);
	cairo_surface_alloc(gui);

	gtk_widget_queue_draw(gui->drawing_area);
}

void maze_solver_cb(int reason, struct MazeGui *gui)
{
	struct Maze *maze = gui->maze;

	if (reason == SOLVER_CB_REASON_SOLVED ||
	    reason == SOLVER_CB_REASON_CANCELED) {
		gtk_widget_set_sensitive(gui->new_button, true);
		gtk_button_set_label(GTK_BUTTON(gui->solve_button), "Solve");

		if (reason == SOLVER_CB_REASON_SOLVED)
			label_set_text(gui->info_label, "Length: %d\nTime: %.3gs",
				       maze_get_path_length(maze),
				       maze_get_solve_time(maze));
	}

	gtk_widget_queue_draw(gui->drawing_area);
}

void on_solve_clicked(GtkButton *button, struct MazeGui *gui)
{
	struct Maze *maze = gui->maze;
	SolverAlgorithm algo;

	if (maze_solver_running(maze)) {
		maze_solve_thread_cancel(maze);
		return;
	}

	algo = gtk_combo_box_get_active(GTK_COMBO_BOX(gui->algo_combo));
	maze_set_solver_algorithm(maze, algo);

	gtk_widget_set_sensitive(gui->new_button, false);
	gtk_button_set_label(GTK_BUTTON(gui->solve_button), "Cancel");
	label_set_text(gui->info_label, "");

	maze_solve_thread(maze, (MazeSolverFunc)maze_solver_cb, gui);
}

void on_draw(GtkDrawingArea *da, cairo_t *cr, struct MazeGui *gui)
{
	GtkAllocation da_rect;
	int cell_width;
	int cell_height;
	int row, col;
	CellColor cell_color;
	GdkRGBA color;
	struct Maze *maze = gui->maze;
	int num_rows;
	int num_cols;
	int surface_width;
	int surface_height;
	double scale_x;
	double scale_y;

	gtk_widget_get_allocated_size(GTK_WIDGET(da), &da_rect, NULL);

	num_rows = maze_get_num_rows(maze);
	num_cols = maze_get_num_cols(maze);

	cell_width = gui->cell_width;
	cell_height = gui->cell_height;

	for (row = 0; row < num_rows; row++) {
		for (col = 0; col < num_cols; col++) {
			GtkAllocation rect;

			cell_color = maze_get_cell_color(maze, row, col);
			get_gdk_color(cell_color, &color);

			rect.x = col * cell_width;
			rect.y = row * cell_height;
			rect.width = cell_width;
			rect.height = cell_height;

			gdk_cairo_set_source_rgba(gui->cr, &color);
			gdk_cairo_rectangle(gui->cr, &rect);

			cairo_fill(gui->cr);
		}
	}

	surface_width = cairo_image_surface_get_width(gui->surface);
	surface_height = cairo_image_surface_get_height(gui->surface);

	scale_x = (double)da_rect.width / surface_width;
	scale_y = (double)da_rect.height / surface_height;

	cairo_scale(cr, scale_x, scale_y);
	cairo_set_source_surface(cr, gui->surface, 0.0, 0.0);
	cairo_paint(cr);
}

static void on_insert_text(GtkEditable *editable, char *new_text,
			   int new_text_length, gpointer position,
			   gpointer user_data)
{
	int i = 0;

	while (new_text[i]) {
		if (!isdigit(new_text[i++])) {
			g_signal_stop_emission_by_name(G_OBJECT(editable),
						       "insert-text");
			return;
		}
	}
}

static void on_scale_changed(GtkRange *range, struct MazeGui *gui)
{
	maze_set_anim_speed(gui->maze, (uint)gtk_range_get_value(range));
}

static void on_destroy(GtkWindow *win, struct MazeGui *gui)
{
	maze_solve_thread_cancel(gui->maze);

	cairo_surface_free(gui);
	g_object_ref(gui->drawing_area);

	gtk_main_quit();
}

static void gui_show(struct MazeGui *gui)
{
	struct Maze *maze = gui->maze;
	GtkWidget *window;
	GtkWidget *hbox;
	GtkWidget *drawing_area;
	GtkWidget *grid;
	GtkWidget *separator;
	GtkWidget *label_rows;
	GtkWidget *label_cols;
	GtkEntry *entry;
	GtkToggleButton *check;
	GtkWidget *button;
	GtkWidget *label;
	GtkComboBoxText *combo;
	GtkWidget *scale;

	cairo_surface_alloc(gui);

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(window), "CMaze");
	g_signal_connect(G_OBJECT(window), "destroy",
			 G_CALLBACK(on_destroy), gui);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
	gtk_container_add(GTK_CONTAINER(window), hbox);

	grid = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
	gtk_box_pack_start(GTK_BOX(hbox), grid, false, false, 6);

	drawing_area = gtk_drawing_area_new();
	gui->drawing_area = g_object_ref(drawing_area);
	gtk_widget_set_size_request(drawing_area, 500, 500);
	gtk_box_pack_start(GTK_BOX(hbox), drawing_area, true, true, 6);
	g_signal_connect(G_OBJECT(drawing_area), "draw",
			 G_CALLBACK(on_draw), gui);

	separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_container_add(GTK_CONTAINER(grid), separator);

	label_rows = gtk_label_new("Rows:");
	gtk_label_set_xalign(GTK_LABEL(label_rows), 1.0);
	gtk_grid_attach_next_to(GTK_GRID(grid), label_rows, separator,
				GTK_POS_BOTTOM, 1, 1);

	entry = GTK_ENTRY(gtk_entry_new());
	gui->entry_num_rows = entry;
	gtk_entry_set_max_length(entry, 3);
	gtk_entry_set_width_chars(entry, 3);
	entry_set_number(entry, maze_get_num_rows(maze));
	g_signal_connect(G_OBJECT(entry), "insert-text",
			 G_CALLBACK(on_insert_text), gui);
	gtk_grid_attach_next_to(GTK_GRID(grid), GTK_WIDGET(entry), label_rows,
				GTK_POS_RIGHT, 1, 1);

	label_cols = gtk_label_new("Cols:");
	gtk_label_set_xalign(GTK_LABEL(label_cols), 1.0);
	gtk_grid_attach_next_to(GTK_GRID(grid), label_cols, label_rows,
				GTK_POS_BOTTOM, 1, 1);

	entry = GTK_ENTRY(gtk_entry_new());
	gui->entry_num_cols = entry;
	gtk_entry_set_max_length(entry, 3);
	gtk_entry_set_width_chars(entry, 3);
	entry_set_number(entry, maze_get_num_cols(maze));
	g_signal_connect(G_OBJECT(entry), "insert-text",
			 G_CALLBACK(on_insert_text), gui);
	gtk_grid_attach_next_to(GTK_GRID(grid), GTK_WIDGET(entry), label_cols,
				GTK_POS_RIGHT, 1, 1);

	check = GTK_TOGGLE_BUTTON(gtk_check_button_new_with_label("Difficult"));
	gui->difficult_check = check;
	gtk_toggle_button_set_active(check, maze_get_difficult(maze));
	gtk_grid_attach_next_to(GTK_GRID(grid), GTK_WIDGET(check), label_cols,
				GTK_POS_BOTTOM, 2, 1);

	button = gtk_button_new_with_label("New");
	gui->new_button = button;
	g_signal_connect(G_OBJECT(button), "clicked",
			 G_CALLBACK(on_new_clicked), gui);
	gtk_grid_attach_next_to(GTK_GRID(grid), button, GTK_WIDGET(check),
				GTK_POS_BOTTOM, 2, 1);

	separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_grid_attach_next_to(GTK_GRID(grid), separator, button,
				GTK_POS_BOTTOM, 2, 1);

	label = gtk_label_new("Solver algorithm:");
	gtk_grid_attach_next_to(GTK_GRID(grid), label, separator,
				GTK_POS_BOTTOM, 1, 1);

	combo = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
	gui->algo_combo = combo;
	gtk_combo_box_text_insert_text(combo, SOLVER_A_STAR, "A Star");
	gtk_combo_box_text_insert_text(combo, SOLVER_ALWAYS_TURN_LEFT, "Always Turn Left");
	gtk_combo_box_text_insert_text(combo, SOLVER_ALWAYS_TURN_RIGHT, "Always Turn Right");
	gtk_combo_box_set_active(GTK_COMBO_BOX(combo),
				 maze_get_solver_algorithm(maze));
	gtk_grid_attach_next_to(GTK_GRID(grid), GTK_WIDGET(combo), label,
				GTK_POS_BOTTOM, 2, 1);

	label = gtk_label_new("Animation speed:");
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_grid_attach_next_to(GTK_GRID(grid), label, GTK_WIDGET(combo),
				GTK_POS_BOTTOM, 2, 1);

	scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
	gtk_scale_set_draw_value(GTK_SCALE(scale), false);
	gtk_range_set_value(GTK_RANGE(scale), maze_get_anim_speed(maze));
	g_signal_connect(G_OBJECT(scale), "value-changed",
			 G_CALLBACK(on_scale_changed), gui);
	gtk_grid_attach_next_to(GTK_GRID(grid), scale, label,
				GTK_POS_BOTTOM, 2, 1);

	button = gtk_button_new_with_label("Solve");
	gui->solve_button = button;
	g_signal_connect(G_OBJECT(button), "clicked",
			 G_CALLBACK(on_solve_clicked), gui);
	gtk_grid_attach_next_to(GTK_GRID(grid), button, GTK_WIDGET(scale),
				GTK_POS_BOTTOM, 2, 1);

	label = gtk_label_new("");
	gui->info_label = GTK_LABEL(label);
	gtk_grid_attach_next_to(GTK_GRID(grid), label, button,
				GTK_POS_BOTTOM, 2, 1);

	gtk_widget_show_all(window);
}

int gtk_maze_run(struct Maze *maze)
{
	struct MazeGui *gui;

	gui = g_malloc0(sizeof(*gui));
	gui->maze = maze;

	gtk_init(0, NULL);
	gui_show(gui);

	gtk_main();

	g_free(gui);

	return 0;
}

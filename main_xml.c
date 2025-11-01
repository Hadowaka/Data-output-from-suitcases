#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

// Структура для хранения временной метки с микросекундами
typedef struct {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int microsecond;
} TimeStamp;

// Структура для хранения данных одного параметра
typedef struct {
    char *name;           // Название параметра
    double *values;       // Массив значений
    TimeStamp *times;     // Массив временных меток
    int data_count;       // Количество точек данных
    double color[3];      // Цвет графика [R, G, B]
    double min_value;     // Минимальное значение в серии
    double max_value;     // Максимальное значение в серии
} DataSeries;

// Основная структура для хранения всех данных графика
typedef struct {
    GtkWidget *drawing_area;
    DataSeries *series;          // Массив параметров
    int series_count;            // Количество параметров
    char *title;
    char *x_label;
    char *y_label;
    char *data_num;              // Номер из XML файла
} GraphData;

// Парсинг строки времени с микросекундами
TimeStamp parse_time_string(const char *time_str) {
    TimeStamp ts = {0};
    
    // Формат с микросекундами: "YYYY-MM-DD HH:MM:SS.mmmmmm"
    if (sscanf(time_str, "%d-%d-%d %d:%d:%d.%d", 
               &ts.year, &ts.month, &ts.day,
               &ts.hour, &ts.minute, &ts.second, &ts.microsecond) == 7) {
        return ts;
    }
    
    // Формат без микросекунд
    if (sscanf(time_str, "%d-%d-%d %d:%d:%d", 
               &ts.year, &ts.month, &ts.day,
               &ts.hour, &ts.minute, &ts.second) == 6) {
        ts.microsecond = 0;
        return ts;
    }
    
    return ts;
}

// Преобразование временной метки в числовое значение
double time_to_double(TimeStamp ts) {
    struct tm time_struct = {0};
    time_struct.tm_year = ts.year - 1900;
    time_struct.tm_mon = ts.month - 1;
    time_struct.tm_mday = ts.day;
    time_struct.tm_hour = ts.hour;
    time_struct.tm_min = ts.minute;
    time_struct.tm_sec = ts.second;
    
    time_t seconds = mktime(&time_struct);
    double total_time = (double)seconds + (double)ts.microsecond / 1000000.0;
    
    return total_time;
}

// Функция для извлечения значения из XML тега
char* extract_xml_value(const char *xml, const char *tag_name) {
    char start_tag[256];
    char end_tag[256];
    snprintf(start_tag, sizeof(start_tag), "<%s>", tag_name);
    snprintf(end_tag, sizeof(end_tag), "</%s>", tag_name);
    
    const char *start = strstr(xml, start_tag);
    if (!start) return NULL;
    
    start += strlen(start_tag);
    const char *end = strstr(start, end_tag);
    if (!end) return NULL;
    
    int length = end - start;
    char *value = malloc(length + 1);
    strncpy(value, start, length);
    value[length] = '\0';
    
    return value;
}

// Проверка на пустой тег (например, <illuminance />)
int is_empty_tag(const char *xml, const char *tag_name) {
    char empty_tag[256];
    snprintf(empty_tag, sizeof(empty_tag), "<%s />", tag_name);
    
    return (strstr(xml, empty_tag) != NULL);
}

// Подсчет количества записей в XML
int count_xml_entries(const char *xml) {
    int count = 0;
    const char *ptr = xml;
    
    // Пропускаем корневой тег <VKID>
    ptr = strstr(ptr, "<data>");
    
    while (ptr != NULL) {
        count++;
        ptr = strstr(ptr + 6, "<data>"); // Длина "<data>"
    }
    
    return count;
}

// Парсинг пользовательского XML формата с корневым тегом <VKID>
gboolean parse_custom_xml(const char *xml_str, GraphData *graph_data) {
    // Создаем 4 параметра для графиков
    graph_data->series_count = 4;
    graph_data->series = malloc(graph_data->series_count * sizeof(DataSeries));
    
    // Инициализация параметров с названиями и цветами
    graph_data->series[0].name = g_strdup("Освещенность");
    graph_data->series[0].color[0] = 1.0;
    graph_data->series[0].color[1] = 0.5;
    graph_data->series[0].color[2] = 0.0;
    
    graph_data->series[1].name = g_strdup("Движение");
    graph_data->series[1].color[0] = 0.0;
    graph_data->series[1].color[1] = 0.7;
    graph_data->series[1].color[2] = 0.0;
    
    graph_data->series[2].name = g_strdup("Температура");
    graph_data->series[2].color[0] = 0.0;
    graph_data->series[2].color[1] = 0.0;
    graph_data->series[2].color[2] = 1.0;
    
    graph_data->series[3].name = g_strdup("Звук");
    graph_data->series[3].color[0] = 0.5;
    graph_data->series[3].color[1] = 0.0;
    graph_data->series[3].color[2] = 0.5;

    // Подсчет количества точек данных
    int data_count = count_xml_entries(xml_str);
    
    if (data_count == 0) {
        g_print("Не найдено записей в XML файле\n");
        return FALSE;
    }

    // Выделение памяти для каждого параметра
    for (int i = 0; i < graph_data->series_count; i++) {
        graph_data->series[i].data_count = data_count;
        graph_data->series[i].values = malloc(data_count * sizeof(double));
        graph_data->series[i].times = malloc(data_count * sizeof(TimeStamp));
        graph_data->series[i].min_value = 1e9;
        graph_data->series[i].max_value = -1e9;
        
        // Инициализация значений по умолчанию
        for (int j = 0; j < data_count; j++) {
            graph_data->series[i].values[j] = 0.0;
        }
    }

    // Заполнение данных из XML
    const char *ptr = xml_str;
    int index = 0;
    
    // Находим первую запись
    ptr = strstr(ptr, "<data>");
    
    while (ptr != NULL && index < data_count) {
        const char *entry_end = strstr(ptr, "</data>");
        if (!entry_end) break;
        
        // Выделяем текущую запись
        int entry_length = entry_end - ptr;
        char *entry = malloc(entry_length + 1);
        strncpy(entry, ptr, entry_length);
        entry[entry_length] = '\0';
        
        // Извлекаем данные
        char *time_str = extract_xml_value(entry, "time");
        char *illuminance_str = extract_xml_value(entry, "illuminance");
        char *motion_str = extract_xml_value(entry, "current_motion");
        char *temp_str = extract_xml_value(entry, "temperature");
        char *sound_str = extract_xml_value(entry, "sound");
        char *num_str = extract_xml_value(entry, "num");
        
        // Обработка временной метки
        if (time_str) {
            TimeStamp ts = parse_time_string(time_str);
            for (int i = 0; i < graph_data->series_count; i++) {
                graph_data->series[i].times[index] = ts;
            }
        }
        
        // Обработка движения
        if (motion_str) {
            double value = atof(motion_str);
            graph_data->series[1].values[index] = value;
            if (value < graph_data->series[1].min_value) graph_data->series[1].min_value = value;
            if (value > graph_data->series[1].max_value) graph_data->series[1].max_value = value;
        } else if (is_empty_tag(entry, "current_motion")) {
            // Пустой тег - используем значение по умолчанию
            graph_data->series[1].values[index] = 0.0;
        }
        
        // Обработка освещенности
        if (illuminance_str) {
            double value = atof(illuminance_str);
            graph_data->series[0].values[index] = value;
            if (value < graph_data->series[0].min_value) graph_data->series[0].min_value = value;
            if (value > graph_data->series[0].max_value) graph_data->series[0].max_value = value;
        } else if (is_empty_tag(entry, "illuminance")) {
            // Пустой тег - используем значение по умолчанию
            graph_data->series[0].values[index] = 0.0;
        }
        
        // Обработка температуры
        if (temp_str) {
            double value = atof(temp_str);
            graph_data->series[2].values[index] = value;
            if (value < graph_data->series[2].min_value) graph_data->series[2].min_value = value;
            if (value > graph_data->series[2].max_value) graph_data->series[2].max_value = value;
        } else if (is_empty_tag(entry, "temperature")) {
            // Пустой тег - используем значение по умолчанию
            graph_data->series[2].values[index] = 0.0;
        }
        
        // Обработка звука
        if (sound_str) {
            double value = atof(sound_str);
            graph_data->series[3].values[index] = value;
            if (value < graph_data->series[3].min_value) graph_data->series[3].min_value = value;
            if (value > graph_data->series[3].max_value) graph_data->series[3].max_value = value;
        } else if (is_empty_tag(entry, "sound")) {
            // Пустой тег - используем значение по умолчанию
            graph_data->series[3].values[index] = 0.0;
        }
        
        // Сохранение номера из первой записи
        if (index == 0 && num_str) {
            graph_data->data_num = g_strdup(num_str);
        }
        
        // Освобождение памяти
        free(entry);
        free(time_str);
        free(illuminance_str);
        free(motion_str);
        free(temp_str);
        free(sound_str);
        free(num_str);
        
        index++;
        
        // Переходим к следующей записи
        ptr = strstr(entry_end + 7, "<data>"); // Длина "</data>"
    }
    
    g_print("Успешно загружено %d записей\n", data_count);
    return TRUE;
}

// Поиск общего диапазона времени для всех серий
void find_time_range(GraphData *graph_data, double *min_time, double *max_time) {
    if (graph_data->series_count == 0) return;
    
    *min_time = time_to_double(graph_data->series[0].times[0]);
    *max_time = time_to_double(graph_data->series[0].times[0]);
    
    for (int i = 0; i < graph_data->series_count; i++) {
        DataSeries *series = &graph_data->series[i];
        for (int j = 0; j < series->data_count; j++) {
            double current_time = time_to_double(series->times[j]);
            if (current_time < *min_time) *min_time = current_time;
            if (current_time > *max_time) *max_time = current_time;
        }
    }
}

// Поиск общего диапазона значений для нормализации
void find_value_range(GraphData *graph_data, double *min_val, double *max_val) {
    if (graph_data->series_count == 0) return;
    
    *min_val = graph_data->series[0].min_value;
    *max_val = graph_data->series[0].max_value;
    
    for (int i = 1; i < graph_data->series_count; i++) {
        if (graph_data->series[i].min_value < *min_val) *min_val = graph_data->series[i].min_value;
        if (graph_data->series[i].max_value > *max_val) *max_val = graph_data->series[i].max_value;
    }
    
    // Если все значения нулевые, устанавливаем разумный диапазон
    if (*min_val == *max_val) {
        *min_val = *min_val - 1.0;
        *max_val = *max_val + 1.0;
    }
}

// Функция отрисовки графиков
gboolean draw_callback(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    GraphData *graph_data = (GraphData *)user_data;
    
    if (graph_data->series_count == 0) return FALSE;

    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);
    int width = allocation.width;
    int height = allocation.height;

    // Определение диапазонов времени и значений
    double min_time, max_time, min_val, max_val;
    find_time_range(graph_data, &min_time, &max_time);
    find_value_range(graph_data, &min_val, &max_val);

    // Добавление отступов для лучшего отображения
    double time_range = max_time - min_time;
    double val_range = max_val - min_val;
    double padding = 0.1;
    
    min_time -= time_range * padding;
    max_time += time_range * padding;
    min_val -= val_range * padding;
    max_val += val_range * padding;

    // Вычисление масштабов
    double scale_x = width / (max_time - min_time);
    double scale_y = height / (max_val - min_val);

    // Очистка области рисования
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);

    // Отрисовка сетки
    cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
    cairo_set_line_width(cr, 0.5);
    
    // Вертикальные линии (время)
    for (int i = 1; i <= 5; i++) {
        double x = (double)width * i / 6.0;
        cairo_move_to(cr, x, 0);
        cairo_line_to(cr, x, height);
    }
    
    // Горизонтальные линии (значения)
    for (int i = 1; i <= 5; i++) {
        double y = (double)height * i / 6.0;
        cairo_move_to(cr, 0, y);
        cairo_line_to(cr, width, y);
    }
    cairo_stroke(cr);

    // Отрисовка осей координат
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_line_width(cr, 1);
    
    // Ось Y
    cairo_move_to(cr, 50, 0);
    cairo_line_to(cr, 50, height - 30);
    
    // Ось X
    cairo_move_to(cr, 50, height - 30);
    cairo_line_to(cr, width, height - 30);
    cairo_stroke(cr);

    // Подписи осей
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 12);
    
    cairo_move_to(cr, 10, height / 2);
    cairo_show_text(cr, "Значения");
    
    cairo_move_to(cr, width / 2 - 20, height - 10);
    cairo_show_text(cr, "Время");

    // Заголовок графика
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 14);
    
    char title[256];
    if (graph_data->data_num) {
        snprintf(title, sizeof(title), "Мониторинг параметров (Номер: %s)", graph_data->data_num);
    } else {
        snprintf(title, sizeof(title), "Мониторинг параметров");
    }
    cairo_move_to(cr, width / 2 - 100, 25);
    cairo_show_text(cr, title);

    // Отрисовка всех графиков
    for (int s = 0; s < graph_data->series_count; s++) {
        DataSeries *series = &graph_data->series[s];
        
        if (series->data_count == 0) continue;
        
        cairo_set_source_rgb(cr, series->color[0], series->color[1], series->color[2]);
        cairo_set_line_width(cr, 2);

        // Построение линий графика
        for (int i = 0; i < series->data_count; i++) {
            double x = 50 + (time_to_double(series->times[i]) - min_time) * scale_x;
            double y = (height - 30) - (series->values[i] - min_val) * scale_y;
            
            if (i == 0) {
                cairo_move_to(cr, x, y);
            } else {
                cairo_line_to(cr, x, y);
            }
        }
        cairo_stroke(cr);

        // Отрисовка точек данных
        for (int i = 0; i < series->data_count; i++) {
            double x = 50 + (time_to_double(series->times[i]) - min_time) * scale_x;
            double y = (height - 30) - (series->values[i] - min_val) * scale_y;
            
            cairo_arc(cr, x, y, 3, 0, 2 * G_PI);
            cairo_fill(cr);
        }
    }

    // Отрисовка легенды
    cairo_set_font_size(cr, 10);
    int legend_y = 50;
    
    for (int s = 0; s < graph_data->series_count; s++) {
        DataSeries *series = &graph_data->series[s];
        
        cairo_set_source_rgb(cr, series->color[0], series->color[1], series->color[2]);
        cairo_rectangle(cr, width - 200, legend_y - 8, 15, 8);
        cairo_fill(cr);
        
        cairo_set_source_rgb(cr, 0, 0, 0);
        cairo_move_to(cr, width - 180, legend_y);
        
        char legend_text[128];
        snprintf(legend_text, sizeof(legend_text), "%s (min: %.1f, max: %.1f)", 
                 series->name, series->min_value, series->max_value);
        cairo_show_text(cr, legend_text);
        
        legend_y += 20;
    }

    // Подписи времени на оси X
    cairo_set_font_size(cr, 9);
    cairo_set_source_rgb(cr, 0, 0, 0);
    
    int num_time_ticks = 5;
    for (int i = 0; i <= num_time_ticks; i++) {
        double time_ratio = (double)i / (double)num_time_ticks;
        double current_time = min_time + time_ratio * (max_time - min_time);
        double x_pos = 50 + (current_time - min_time) * scale_x;
        
        time_t raw_time = (time_t)current_time;
        struct tm *time_info = localtime(&raw_time);
        
        char time_label[32];
        snprintf(time_label, sizeof(time_label), "%02d:%02d", 
                 time_info->tm_hour, time_info->tm_min);
        
        cairo_move_to(cr, x_pos - 10, height - 15);
        cairo_show_text(cr, time_label);
        
        cairo_move_to(cr, x_pos, height - 35);
        cairo_line_to(cr, x_pos, height - 25);
        cairo_stroke(cr);
    }

    // Подписи значений на оси Y
    int num_val_ticks = 5;
    for (int i = 0; i <= num_val_ticks; i++) {
        double val_ratio = (double)i / (double)num_val_ticks;
        double current_val = min_val + val_ratio * (max_val - min_val);
        double y_pos = (height - 30) - (current_val - min_val) * scale_y;
        
        char val_label[32];
        snprintf(val_label, sizeof(val_label), "%.1f", current_val);
        
        cairo_move_to(cr, 25, y_pos + 3);
        cairo_show_text(cr, val_label);
        
        cairo_move_to(cr, 45, y_pos);
        cairo_line_to(cr, 55, y_pos);
        cairo_stroke(cr);
    }

    return FALSE;
}

// Загрузка XML данных из файла
gboolean load_xml_from_file(const char *filename, GraphData *graph_data) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        g_print("Не удалось открыть файл: %s\n", filename);
        return FALSE;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *xml_str = malloc(file_size + 1);
    fread(xml_str, 1, file_size, file);
    xml_str[file_size] = '\0';
    fclose(file);

    gboolean result = parse_custom_xml(xml_str, graph_data);
    free(xml_str);
    return result;
}

// Освобождение памяти, занятой данными графика
void free_graph_data(GraphData *graph_data) {
    for (int i = 0; i < graph_data->series_count; i++) {
        free(graph_data->series[i].values);
        free(graph_data->series[i].times);
        g_free(graph_data->series[i].name);
    }
    free(graph_data->series);
    g_free(graph_data->title);
    g_free(graph_data->x_label);
    g_free(graph_data->y_label);
    g_free(graph_data->data_num);
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "График датчиков");
    gtk_window_set_default_size(GTK_WINDOW(window), 1000, 700);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GraphData graph_data = {0};
    graph_data.drawing_area = gtk_drawing_area_new();
    g_signal_connect(graph_data.drawing_area, "draw", 
                    G_CALLBACK(draw_callback), &graph_data);

    if (argc > 1) {
        if (!load_xml_from_file(argv[1], &graph_data)) {
            g_print("Ошибка загрузки файла: %s\n", argv[1]);
            return 1;
        }
    } else {
        g_print("Использование: %s <xml-файл>\n", argv[0]);
        return 1;
    }

    gtk_container_add(GTK_CONTAINER(window), graph_data.drawing_area);
    gtk_widget_show_all(window);
    gtk_main();

    free_graph_data(&graph_data);
    return 0;
}
#include <gtk/gtk.h>
#include <json-c/json.h>
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
    char *name;           // Название параметра (illuminance, temperature, etc.)
    double *values;       // Массив значений
    TimeStamp *times;     // Массив временных меток
    int data_count;       // Количество точек
    double color[3];      // Цвет графика [R, G, B]
    double min_value;     // Минимальное значение
    double max_value;     // Максимальное значение
} DataSeries;

// Основная структура для хранения всех данных
typedef struct {
    GtkWidget *drawing_area;
    DataSeries *series;          // Массив параметров
    int series_count;            // Количество параметров
    char *title;
    char *x_label;
    char *y_label;
    char *data_num;              // Номер из JSON (константа)
    int graph_type;              // 0-линейный, 1-столбчатый, 2-круговой, 3-точечный
    int series_index;            // Индекс серии данных для этого графика
} GraphData;

// Функция для парсинга времени с микросекундами
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

// Функция для преобразования времени в число
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

// Функция для получения double из JSON объекта (обрабатывает строки и числа)
double get_json_double(struct json_object *obj) {
    if (json_object_is_type(obj, json_type_double)) {
        return json_object_get_double(obj);
    } else if (json_object_is_type(obj, json_type_int)) {
        return (double)json_object_get_int(obj);
    } else if (json_object_is_type(obj, json_type_string)) {
        const char *str = json_object_get_string(obj);
        return atof(str); // Преобразуем строку в double
    }
    return 0.0;
}

// Функция для парсинга JSON без повторного использования макроса
gboolean parse_custom_json(const char *json_str, GraphData *graph_data) {
    struct json_object *root = json_tokener_parse(json_str);
    if (!root) {
        g_print("Ошибка парсинга JSON\n");
        return FALSE;
    }

    // Создаем 4 параметра для графиков
    graph_data->series_count = 4;
    graph_data->series = malloc(graph_data->series_count * sizeof(DataSeries));
    
    // Инициализируем параметры
    // Освещенность
    graph_data->series[0].name = g_strdup("Освещенность");
    graph_data->series[0].color[0] = 1.0; // Красный
    graph_data->series[0].color[1] = 0.5;
    graph_data->series[0].color[2] = 0.0;
    
    // Движение
    graph_data->series[1].name = g_strdup("Движение");
    graph_data->series[1].color[0] = 0.0; // Зеленый
    graph_data->series[1].color[1] = 0.7;
    graph_data->series[1].color[2] = 0.0;
    
    // Температура
    graph_data->series[2].name = g_strdup("Температура");
    graph_data->series[2].color[0] = 0.0; // Синий
    graph_data->series[2].color[1] = 0.0;
    graph_data->series[2].color[2] = 1.0;
    
    // Звук
    graph_data->series[3].name = g_strdup("Звук");
    graph_data->series[3].color[0] = 0.5; // Фиолетовый
    graph_data->series[3].color[1] = 0.0;
    graph_data->series[3].color[2] = 0.5;

    // Подсчитываем количество точек данных
    int data_count = 0;
    struct json_object *obj;
    struct lh_entry *entry;
    
    struct lh_table *json_table = json_object_get_object(root);
    entry = json_table->head;
    
    while (entry) {
        const char *key = (const char *)entry->k;
        if (atoi(key) > 0) {
            data_count++;
        }
        entry = entry->next;
    }

    // Выделяем память для каждого параметра
    for (int i = 0; i < graph_data->series_count; i++) {
        graph_data->series[i].data_count = data_count;
        graph_data->series[i].values = malloc(data_count * sizeof(double));
        graph_data->series[i].times = malloc(data_count * sizeof(TimeStamp));
        graph_data->series[i].min_value = 1e9;
        graph_data->series[i].max_value = -1e9;
    }

    // Заполняем данные из JSON
    int index = 0;
    entry = json_table->head;
    
    while (entry) {
        const char *key = (const char *)entry->k;
        struct json_object *val = (struct json_object *)entry->v;
        
        if (atoi(key) > 0) {
            struct json_object *time_obj, *motion_obj, *illuminance_obj, *temp_obj, *sound_obj, *num_obj;
            
            // Время
            if (json_object_object_get_ex(val, "time", &time_obj)) {
                const char *time_str = json_object_get_string(time_obj);
                TimeStamp ts = parse_time_string(time_str);
                
                // Сохраняем время для всех параметров
                for (int i = 0; i < graph_data->series_count; i++) {
                    graph_data->series[i].times[index] = ts;
                }
            }
            
            // Освещенность (illuminance)
            if (json_object_object_get_ex(val, "illuminance", &illuminance_obj)) {
                double value = get_json_double(illuminance_obj);
                graph_data->series[0].values[index] = value;
                if (value < graph_data->series[0].min_value) graph_data->series[0].min_value = value;
                if (value > graph_data->series[0].max_value) graph_data->series[0].max_value = value;
            }
            
            // Движение (current_motion)
            if (json_object_object_get_ex(val, "current_motion", &motion_obj)) {
                double value = get_json_double(motion_obj);
                graph_data->series[1].values[index] = value;
                if (value < graph_data->series[1].min_value) graph_data->series[1].min_value = value;
                if (value > graph_data->series[1].max_value) graph_data->series[1].max_value = value;
            }
            
            // Температура (temperature)
            if (json_object_object_get_ex(val, "temperature", &temp_obj)) {
                double value = get_json_double(temp_obj);
                graph_data->series[2].values[index] = value;
                if (value < graph_data->series[2].min_value) graph_data->series[2].min_value = value;
                if (value > graph_data->series[2].max_value) graph_data->series[2].max_value = value;
            }
            
            // Звук (sound)
            if (json_object_object_get_ex(val, "sound", &sound_obj)) {
                double value = get_json_double(sound_obj);
                graph_data->series[3].values[index] = value;
                if (value < graph_data->series[3].min_value) graph_data->series[3].min_value = value;
                if (value > graph_data->series[3].max_value) graph_data->series[3].max_value = value;
            }
            
            // Номер (num) - берем из первой точки
            if (index == 0 && json_object_object_get_ex(val, "num", &num_obj)) {
                const char *num_str = json_object_get_string(num_obj);
                graph_data->data_num = g_strdup(num_str);
            }
            
            index++;
        }
        entry = entry->next;
    }
    
    json_object_put(root);
    return TRUE;
}

// Функция для поиска диапазона времени для одного графика
void find_time_range_single(GraphData *graph_data, double *min_time, double *max_time, int series_index) {
    if (graph_data->series_count == 0 || series_index >= graph_data->series_count) return;
    
    DataSeries *series = &graph_data->series[series_index];
    if (series->data_count == 0) return;
    
    *min_time = time_to_double(series->times[0]);
    *max_time = time_to_double(series->times[0]);
    
    for (int j = 0; j < series->data_count; j++) {
        double current_time = time_to_double(series->times[j]);
        if (current_time < *min_time) *min_time = current_time;
        if (current_time > *max_time) *max_time = current_time;
    }
}

// Функция для поиска диапазона значений для одного графика
void find_value_range_single(GraphData *graph_data, double *min_val, double *max_val, int series_index) {
    if (graph_data->series_count == 0 || series_index >= graph_data->series_count) return;
    
    DataSeries *series = &graph_data->series[series_index];
    *min_val = series->min_value;
    *max_val = series->max_value;
}

// Функция для получения названия типа графика
const char* get_graph_type_name(int graph_type) {
    switch(graph_type) {
        case 0: return "Линейный график";
        case 1: return "Столбчатая диаграмма";
        case 2: return "Круговая диаграмма";
        case 3: return "Точечный график";
        default: return "График";
    }
}

// Функция для получения названия параметра по индексу
const char* get_parameter_name(int series_index) {
    switch(series_index) {
        case 0: return "Освещенность";
        case 1: return "Движение";
        case 2: return "Температура";
        case 3: return "Звук";
        default: return "Параметр";
    }
}

// Функция отрисовки одного графика
gboolean draw_single_callback(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    GraphData *graph_data = (GraphData *)user_data;
    
    if (graph_data->series_count == 0) return FALSE;

    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);
    int width = allocation.width;
    int height = allocation.height;

    // Определяем какой параметр отображать на этом графике
    int series_index = graph_data->series_index;
    DataSeries *series = &graph_data->series[series_index];

    if (series->data_count == 0) return FALSE;

    // Находим диапазон времени и значений для этого параметра
    double min_time, max_time, min_val, max_val;
    find_time_range_single(graph_data, &min_time, &max_time, series_index);
    find_value_range_single(graph_data, &min_val, &max_val, series_index);

    // Добавляем отступы
    double time_range = max_time - min_time;
    double val_range = max_val - min_val;
    if (time_range == 0) time_range = 1;
    if (val_range == 0) val_range = 1;
    
    double padding = 0.1;
    
    min_time -= time_range * padding;
    max_time += time_range * padding;
    min_val -= val_range * padding;
    max_val += val_range * padding;

    // Вычисляем масштаб
    double scale_x = (width - 100) / (max_time - min_time);
    double scale_y = (height - 80) / (max_val - min_val);

    // Очищаем область
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);

    // Рисуем сетку
    cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
    cairo_set_line_width(cr, 0.5);
    
    // Вертикальные линии (время)
    for (int i = 1; i <= 5; i++) {
        double x = 50 + (double)(width - 100) * i / 6.0;
        cairo_move_to(cr, x, 20);
        cairo_line_to(cr, x, height - 60);
    }
    
    // Горизонтальные линии (значения)
    for (int i = 1; i <= 5; i++) {
        double y = 20 + (double)(height - 80) * i / 6.0;
        cairo_move_to(cr, 50, y);
        cairo_line_to(cr, width - 50, y);
    }
    cairo_stroke(cr);

    // Рисуем оси координат
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_line_width(cr, 1);
    
    // Ось Y
    cairo_move_to(cr, 50, 20);
    cairo_line_to(cr, 50, height - 60);
    
    // Ось X
    cairo_move_to(cr, 50, height - 60);
    cairo_line_to(cr, width - 50, height - 60);
    cairo_stroke(cr);

    // Рисуем подписи осей
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 12);
    
    // Подпись оси Y
    cairo_move_to(cr, 10, height / 2);
    cairo_show_text(cr, "Значения");
    
    // Подпись оси X
    cairo_move_to(cr, width / 2 - 20, height - 10);
    cairo_show_text(cr, "Время");

    // Рисуем заголовок графика
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 14);
    
    char title[256];
    const char* graph_type_name = get_graph_type_name(graph_data->graph_type);
    const char* param_name = get_parameter_name(series_index);
    
    if (graph_data->data_num) {
        snprintf(title, sizeof(title), "%s: %s (Номер: %s)", graph_type_name, param_name, graph_data->data_num);
    } else {
        snprintf(title, sizeof(title), "%s: %s", graph_type_name, param_name);
    }
    cairo_move_to(cr, width / 2 - 150, 15);
    cairo_show_text(cr, title);

    // Рисуем график в зависимости от типа
    cairo_set_source_rgb(cr, series->color[0], series->color[1], series->color[2]);
    
    switch(graph_data->graph_type) {
        case 0: // Линейный график - для Температуры
            cairo_set_line_width(cr, 2);
            for (int i = 0; i < series->data_count; i++) {
                double x = 50 + (time_to_double(series->times[i]) - min_time) * scale_x;
                double y = (height - 60) - (series->values[i] - min_val) * scale_y;
                
                if (i == 0) {
                    cairo_move_to(cr, x, y);
                } else {
                    cairo_line_to(cr, x, y);
                }
            }
            cairo_stroke(cr);
            
            // Рисуем точки
            for (int i = 0; i < series->data_count; i++) {
                double x = 50 + (time_to_double(series->times[i]) - min_time) * scale_x;
                double y = (height - 60) - (series->values[i] - min_val) * scale_y;
                
                cairo_arc(cr, x, y, 3, 0, 2 * G_PI);
                cairo_fill(cr);
            }
            break;
            
        case 1: // Столбчатая диаграмма - для Движения
            for (int i = 0; i < series->data_count; i++) {
                double x = 50 + (time_to_double(series->times[i]) - min_time) * scale_x;
                double bar_width = (width - 100) / series->data_count * 0.6;
                double bar_height = (series->values[i] - min_val) * scale_y;
                
                cairo_rectangle(cr, x - bar_width/2, height - 60 - bar_height, bar_width, bar_height);
                cairo_fill(cr);
            }
            break;
            
case 2: // Круговая диаграмма - для Звука (как напряжения)
    {
        // Группируем уникальные значения и считаем их количество
        double unique_values[100] = {0};
        int value_counts[100] = {0};
        int unique_count = 0;
        
        for (int i = 0; i < series->data_count; i++) {
            double current_value = series->values[i];
            int found = 0;
            
            // Ищем, есть ли уже такое значение
            for (int j = 0; j < unique_count; j++) {
                if (fabs(unique_values[j] - current_value) < 0.001) {
                    value_counts[j]++;
                    found = 1;
                    break;
                }
            }
            
            // Если не нашли, добавляем новое значение
            if (!found && unique_count < 100) {
                unique_values[unique_count] = current_value;
                value_counts[unique_count] = 1;
                unique_count++;
            }
        }
        
        // Считаем общее количество точек для пропорций
        int total_points = series->data_count;
        
        if (total_points > 0) {
            // Правильный расчет центра и радиуса
            double center_x = width / 2;
            double center_y = height / 2;
            double available_radius = fmin(width, height) / 3;
            double radius = available_radius;
            double start_angle = 0;
            
            // Разные цвета для каждой секции
            double colors[][3] = {
                {1.0, 0.0, 0.0},   // Красный
                {0.0, 0.8, 0.0},   // Зеленый
                {0.0, 0.0, 1.0},   // Синий
                {1.0, 1.0, 0.0},   // Желтый
                {1.0, 0.0, 1.0},   // Пурпурный
                {0.0, 1.0, 1.0},   // Голубой
                {1.0, 0.5, 0.0},   // Оранжевый
                {0.5, 0.0, 0.5},   // Фиолетовый
                {0.5, 0.5, 0.0},   // Оливковый
                {0.0, 0.5, 0.5}    // Бирюзовый
            };
            int color_count = 10;
            
            // Рисуем секции для уникальных значений
            for (int i = 0; i < unique_count; i++) {
                // Размер секции пропорционален количеству точек с этим значением
                double slice_angle = 2 * G_PI * value_counts[i] / total_points;
                
                // Выбираем разный цвет для каждой секции
                double r = colors[i % color_count][0];
                double g = colors[i % color_count][1];
                double b = colors[i % color_count][2];
                
                cairo_set_source_rgb(cr, r, g, b);
                
                cairo_move_to(cr, center_x, center_y);
                cairo_arc(cr, center_x, center_y, radius, start_angle, start_angle + slice_angle);
                cairo_close_path(cr);
                cairo_fill(cr);
                
                // Рисуем границу секции
                cairo_set_source_rgb(cr, 0, 0, 0);
                cairo_set_line_width(cr, 1);
                cairo_move_to(cr, center_x, center_y);
                cairo_arc(cr, center_x, center_y, radius, start_angle, start_angle + slice_angle);
                cairo_close_path(cr);
                cairo_stroke(cr);
                
                // Подпись секции
                if (slice_angle > 0.1) { // Подписываем только достаточно большие секции
                    double mid_angle = start_angle + slice_angle / 2;
                    double text_radius = radius * 0.7;
                    double text_x = center_x + text_radius * cos(mid_angle);
                    double text_y = center_y + text_radius * sin(mid_angle);
                    
                    // Подготавливаем текст: значение и количество
                    char value_text[64];
                    snprintf(value_text, sizeof(value_text), "%.1f\n(%d)", 
                             unique_values[i], value_counts[i]);
                    
                    // Белый текст для темных секций, черный для светлых
                    double brightness = (r + g + b) / 3.0;
                    if (brightness < 0.5) {
                        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0); // Белый
                    } else {
                        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0); // Черный
                    }
                    
                    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
                    cairo_set_font_size(cr, 9);
                    
                    // Центрируем текст
                    cairo_text_extents_t extents;
                    cairo_text_extents(cr, value_text, &extents);
                    cairo_move_to(cr, text_x - extents.width/2, text_y + extents.height/2);
                    cairo_show_text(cr, value_text);
                }
                
                start_angle += slice_angle;
            }
            
            // Внешняя граница круга
            cairo_set_source_rgb(cr, 0, 0, 0);
            cairo_set_line_width(cr, 2);
            cairo_arc(cr, center_x, center_y, radius, 0, 2 * G_PI);
            cairo_stroke(cr);
            
            // Подпись в центре круга
            cairo_set_source_rgb(cr, 0, 0, 0);
            cairo_set_font_size(cr, 12);
            char total_text[32];
            snprintf(total_text, sizeof(total_text), "Всего: %d", total_points);
            cairo_text_extents_t total_extents;
            cairo_text_extents(cr, total_text, &total_extents);
            cairo_move_to(cr, center_x - total_extents.width/2, center_y + total_extents.height/2);
            cairo_show_text(cr, total_text);
        }
        else {
            // Если нет данных
            cairo_set_source_rgb(cr, 0.8, 0.8, 0.8);
            cairo_arc(cr, width/2, height/2, fmin(width, height)/3, 0, 2 * G_PI);
            cairo_fill(cr);
            cairo_set_source_rgb(cr, 0, 0, 0);
            cairo_set_line_width(cr, 2);
            cairo_arc(cr, width/2, height/2, fmin(width, height)/3, 0, 2 * G_PI);
            cairo_stroke(cr);
            
            cairo_set_source_rgb(cr, 0, 0, 0);
            cairo_set_font_size(cr, 14);
            cairo_move_to(cr, width/2 - 40, height/2);
            cairo_show_text(cr, "Нет данных");
        }
    }
    break;
            
        case 3: // Точечный график - для Освещенности
            for (int i = 0; i < series->data_count; i++) {
                double x = 50 + (time_to_double(series->times[i]) - min_time) * scale_x;
                double y = (height - 60) - (series->values[i] - min_val) * scale_y;
                
                // Размер точки зависит от значения
                double point_size = 2 + (series->values[i] - min_val) / (max_val - min_val) * 2;
                cairo_arc(cr, x, y, point_size, 0, 2 * G_PI);
                cairo_fill(cr);
            }
            break;
    }

    // Рисуем подписи времени на оси X (только для графиков, где есть время)
    if (graph_data->graph_type != 2) { // Не для круговой диаграммы
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
            
            cairo_move_to(cr, x_pos - 10, height - 45);
            cairo_show_text(cr, time_label);
            
            // Черточки на оси
            cairo_move_to(cr, x_pos, height - 65);
            cairo_line_to(cr, x_pos, height - 55);
            cairo_stroke(cr);
        }
    }

    // Рисуем подписи значений на оси Y (только для графиков с осями)
    if (graph_data->graph_type != 2) { // Не для круговой диаграммы
        int num_val_ticks = 5;
        for (int i = 0; i <= num_val_ticks; i++) {
            double val_ratio = (double)i / (double)num_val_ticks;
            double current_val = min_val + val_ratio * (max_val - min_val);
            double y_pos = (height - 60) - (current_val - min_val) * scale_y;
            
            char val_label[32];
            snprintf(val_label, sizeof(val_label), "%.1f", current_val);
            
            cairo_move_to(cr, 25, y_pos + 3);
            cairo_show_text(cr, val_label);
            
            // Черточки на оси
            cairo_move_to(cr, 45, y_pos);
            cairo_line_to(cr, 55, y_pos);
            cairo_stroke(cr);
        }
    }

    // Рисуем статистику в углу
    cairo_set_font_size(cr, 10);
    cairo_set_source_rgb(cr, 0, 0, 0);
    
    char stats[128];
    snprintf(stats, sizeof(stats), "min: %.2f, max: %.2f, точек: %d", 
             series->min_value, series->max_value, series->data_count);
    cairo_move_to(cr, width - 200, 30);
    cairo_show_text(cr, stats);

    return FALSE;
}

// Функция для загрузки JSON из файла
gboolean load_json_from_file(const char *filename, GraphData *graph_data) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        g_print("Не удалось открыть файл: %s\n", filename);
        return FALSE;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *json_str = malloc(file_size + 1);
    fread(json_str, 1, file_size, file);
    json_str[file_size] = '\0';
    fclose(file);

    gboolean result = parse_custom_json(json_str, graph_data);
    free(json_str);
    return result;
}

// Функция для освобождения памяти
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
    gtk_window_set_title(GTK_WINDOW(window), "Мониторинг сенсоров - 4 типа графиков");
    gtk_window_set_default_size(GTK_WINDOW(window), 1200, 800);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    // Создаем основной контейнер
    GtkWidget *grid = gtk_grid_new();
    gtk_container_add(GTK_CONTAINER(window), grid);

    // Загружаем данные
    GraphData graph_data = {0};
    if (argc > 1) {
        if (!load_json_from_file(argv[1], &graph_data)) {
            g_print("Ошибка загрузки файла: %s\n", argv[1]);
            return 1;
        }
    } else {
        g_print("Использование: %s <json-файл>\n", argv[0]);
        return 1;
    }

    // Создаем 4 области для рисования с разными типами графиков
    GtkWidget *drawing_areas[4];
    GraphData graph_data_array[4];

    // Настройка графиков:
    // 0: Линейный график для Температуры
    // 1: Столбчатая диаграмма для Движения  
    // 2: Круговая диаграмма для Звука (как напряжения)
    // 3: Точечный график для Освещенности
    
    for (int i = 0; i < 4; i++) {
        // Копируем данные для каждого графика
        graph_data_array[i] = graph_data;
        graph_data_array[i].graph_type = i; // Устанавливаем тип графика
        graph_data_array[i].series_index = i; // Устанавливаем индекс данных
        
        drawing_areas[i] = gtk_drawing_area_new();
        gtk_widget_set_size_request(drawing_areas[i], 550, 350);
        g_signal_connect(drawing_areas[i], "draw", 
                        G_CALLBACK(draw_single_callback), &graph_data_array[i]);
        
        // Размещаем в сетке 2x2
        gtk_grid_attach(GTK_GRID(grid), drawing_areas[i], i % 2, i / 2, 1, 1);
    }

    gtk_widget_show_all(window);
    gtk_main();

    // Освобождаем память (достаточно освободить одну копию, так как данные одинаковые)
    free_graph_data(&graph_data);
    
    return 0;
}

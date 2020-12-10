#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "list.h"
#include "obj_parser.h"


char *skip_whitespace(char *str)
{
    if (*str == ' ')
    {
        str++;
        return skip_whitespace(str);
    }
    return str;
}

// Assuming fileptr is at buffer + 1 in the file
int backtrack_fileptr(FILE *fp, char *buffer, size_t buf_size, char key)
{
    for (int i = (buf_size-1); i >= 0; i--)
    {
        if (buffer[i] == key)
        {
            // Sets the file pointer to the last instance of key + 1
            int offset = -1 * ((buf_size - i) +  1);
            fseek(fp, offset, SEEK_CUR);
            return i;
        }
    }
    return buf_size;
}

// Parses vertex coords, texcoords, and uv coords
void parse_coordinate(char *line, double ret[], int n_values)
{
    int idx = 0;
    for (;; line++)
    {
        if(*line == ' ')
        {
            line = skip_whitespace(line);
            char *tmp;
            ret[idx] = strtod(line, &tmp);
            idx++;
        }
        if(idx == n_values)
        {
            break;
        }
    }
}

// v1[/vt1][/vn1] ...
// v1//vn1
// v1/vt1
void parse_idx_cluster(char *start, int ret[3])
{
    int slash_offsets[2] = {0}; // max 2 slashes, min 0
    int idx = 0;

    for (int i = 0; ((start[i] != ' ') && (start[i] != '\0')); i++)
    {
        if (start[i] == '/')
        {
            slash_offsets[idx] = i;
            idx++;
            if (idx == 2)
            {
                break;
            }
        }
    }

    ret[0] = atoi(start);

    if (slash_offsets[1] - slash_offsets[0] == 1) // two adjacent slashes
    {
        ret[2] = atoi(&(start[slash_offsets[1] + 1]));
        return;
    }

    for (int i = 0; i < idx; i++)
    {
        ret[i+1] = atoi(&(start[slash_offsets[i] + 1]));
    }
}

void parse_idx_line(char *line_start_ptr, list *out_values)
{
    line_start_ptr++; // skipping first char 'f'
    line_start_ptr = skip_whitespace(line_start_ptr);
    char* start = line_start_ptr;

    // This isn't bad, I promise
    for (;; line_start_ptr++)
    {
        bool is_nullc = (*line_start_ptr == '\0');
        if (*line_start_ptr == ' ' || is_nullc)
        {
            line_start_ptr = skip_whitespace(line_start_ptr);
            int tmp[3] = {-1, -1, -1};
            parse_idx_cluster(start, tmp);

            start = line_start_ptr;
            append_to_list(out_values, tmp);
        }
        if (is_nullc)
        {
            break;
        }
    }
}

// https://notes.underscorediscovery.com/obj-parser-easy-parse-time-triangulation/
void triangulate(list *face_verts, list *out_list)
{
    for (int i = 1; i < face_verts->used - 1; i++)
    {
        int *corner0 = index_list(face_verts, 0);
        int *corner1 = index_list(face_verts, i);
        int *corner2 = index_list(face_verts, i + 1);
        append_to_list(out_list, corner0);
        append_to_list(out_list, corner1);
        append_to_list(out_list, corner2);
    }
}

void parse_obj(char *filename, size_t *out_size,
        double (**out_verts)[3],
        double (**out_texcoords)[2],
        double (**out_normals)[3])
{
    list verts;
    list texcoords;
    list normals;
    list ids;

    init_list(&verts, sizeof(double[3]), OBJ_LIST_SIZE);
    init_list(&texcoords, sizeof(double[2]), OBJ_LIST_SIZE);
    init_list(&normals, sizeof(double[3]), OBJ_LIST_SIZE);
    init_list(&ids, sizeof(int[3]), OBJ_LIST_SIZE);

    size_t buf_size = sizeof(char) * OBJ_FILE_BUF_SIZE;
    char *buffer = calloc(OBJ_FILE_BUF_SIZE, sizeof(char));
    if (buffer == NULL)
    {
        perror("malloc failed");
        exit(1);
    }

    FILE *fp = fopen(filename, "r");
    if (fp == NULL)
    {
        perror("Unable to open file");
        exit(1);
    }

    for(;;)
    {
        // reading buf_size - 1 to leave space for null term
        int read_size = fread(buffer, sizeof(char), buf_size - 1, fp);
        int newline_idx = backtrack_fileptr(fp, buffer, buf_size, '\n');
        buffer[newline_idx + 1] = '\0';

        // iterating on each line of the buffer, mutates the buffer
        char *delim = "\n";
        char *line_ptr = strtok(buffer, delim);
        while (line_ptr != NULL)
        {
            line_ptr = skip_whitespace(line_ptr);

            // vertex x y z
            if (memcmp(line_ptr, "v ", 2) == 0)
            {
                double tmp[3];
                parse_coordinate(line_ptr, tmp, 3);
                append_to_list(&verts, tmp);
            }
            // texcoord x y
            else if (memcmp(line_ptr, "vt", 2) == 0)
            {
                double tmp[2];
                parse_coordinate(line_ptr, tmp, 2);
                append_to_list(&texcoords, tmp);
            }
            // normal x y z
            else if (memcmp(line_ptr, "vn", 2) == 0)
            {
                double tmp[3];
                parse_coordinate(line_ptr, tmp, 3);
                append_to_list(&normals, tmp);
            }
            // face point indexes idx/idx/idx ...
            else if (memcmp(line_ptr, "f ", 2) == 0)
            {
                list line_values;
                init_list(&line_values, sizeof(int[3]), 4); // Assuming <=4 verts per face
                parse_idx_line(line_ptr, &line_values);

                // doesn't change data if line is already a tri
                triangulate(&line_values, &ids);


                free(line_values.array);
            }

            line_ptr = strtok(NULL, delim);
        }

        // EOF
        if (read_size < buf_size - 1)
        {
            break;
        }
    }
    free(buffer);
    fclose(fp);

    // Populating output data
    *out_size = ids.used;

    *out_verts = malloc(sizeof(double[3]) * *out_size);
    *out_texcoords = malloc(sizeof(double[2]) * *out_size);
    *out_normals = malloc(sizeof(double[3]) * *out_size);
    if (out_verts == NULL || out_texcoords == NULL || out_normals == NULL)
    {
        perror("malloc failed");
        exit(1);
    }

    for (int i = 0; i <ids.used; i++)
    {
        int *cluster = index_list(&ids, i);
        int v_idx = cluster[0];
        int vt_idx = cluster[1];
        int vn_idx = cluster[2];

        double *v_ptr = index_list(&verts, v_idx - 1);
        memcpy((*out_verts)[i], v_ptr, sizeof(double[3]));

        // vt and vn are optional
        if (vt_idx != -1)
        {
            double *vt_ptr = index_list(&texcoords, vt_idx - 1);
            memcpy((*out_texcoords)[i], vt_ptr, sizeof(double[2]));
        }
        if (vn_idx != -1)
        {
            double *vn_ptr = index_list(&normals, vn_idx - 1);
            memcpy((*out_normals)[i], vn_ptr, sizeof(double[3]));
        }
    }

    // Cleanup
    free(verts.array);
    free(texcoords.array);
    free(normals.array);
    free(ids.array);
}


/**
    \file box.c
    \author Sachith Dunatunga
    \date 04.06.12

    A boundary condition file for a 2D box. You can set the dimensions as well
    as the friction boundary conditions as properties in the configuration file.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "interpolate.h"
#include "particle.h"
#include "node.h"
#include "process.h"
#include "material.h"
#include "element.h"

#define XFRICTION 0x01
#define YFRICTION 0x02

static size_t left_col = 0;
static size_t right_col = 0;
static size_t bottom_row = 0;
static size_t top_row = 0;

static int left_fric = 0;
static int right_fric = 0;
static int bottom_fric = 0;
static int top_fric = 0;

void generate_dirichlet_bcs(job_t *job);
void generate_node_number_override(job_t *job);

void bc_init(job_t *job)
{
    return;
}

/* Returns 0 for failure, anything else for success (typically 1). */
int bc_validate(job_t *job)
{
    double x0, y0, w, h;

    if (job->boundary.num_fp64_props != 4) {
        fprintf(stderr, "%s:%s: Wrong number of floating point boundary condition properties. Expected 4, got %zu.\n", __FILE__, __func__, job->boundary.num_fp64_props);
        return 0;
    }

    if (job->boundary.num_int_props != 4) {
        fprintf(stderr, "%s:%s: Wrong number of integer boundary condition properties. Expected 4, got %zu.\n", __FILE__, __func__, job->boundary.num_int_props);
        return 0;
    }

    x0 = job->boundary.fp64_props[0];
    y0 = job->boundary.fp64_props[1];
    w = job->boundary.fp64_props[2];
    h = job->boundary.fp64_props[3];
    
    left_fric = job->boundary.int_props[0];
    bottom_fric = job->boundary.int_props[1];
    right_fric = job->boundary.int_props[2];
    top_fric = job->boundary.int_props[3];
    
    printf("%s:%s: (x_bottomleft, y_bottomleft): (%g, %g), (width, height): (%g, %g)\n",
        __FILE__, __func__ , x0, y0, w, h);

    printf("%s:%s: friction on (left, bottom, right, top): (%d, %d, %d, %d)\n",
        __FILE__, __func__ , left_fric, bottom_fric, right_fric, top_fric);

    if (x0 < 0 || y0 < 0 || x0 > 1.0 || y0 > 1.0) {
        fprintf(stderr, "%s:%s: Box must start in [(0,0),(1,1)]\n", __FILE__, __func__);
        return 0;
    }

    if (w < 0 || h < 0) {
        fprintf(stderr, "%s:%s: Width and Height must be positive.\n", __FILE__, __func__);
        return 0;
    }

    if ((x0 + w) > 1.0 || (y0 + h) > 1.0) {
        fprintf(stderr, "%s:%s: Box must be contained in [(0,0), (1,1)] .\n", __FILE__, __func__);
        return 0;
    }

    left_col = x0 * (job->N - 1);
    right_col = (x0 + w) * (job->N - 1);
    bottom_row = y0 * (job->N - 1);
    top_row = (y0 + h) * (job->N - 1);

    printf("%s:%s: (left_col, bottom_row): (%zu, %zu), (right_col, top_row): (%zu, %zu)\n",
        __FILE__, __func__ , left_col, bottom_row, right_col, top_row);
    
    return 1;
}

void bc_time_varying(job_t *job)
{
    generate_dirichlet_bcs(job);
    generate_node_number_override(job);
    return;
}

/*----------------------------------------------------------------------------*/
void generate_dirichlet_bcs(job_t *job)
{
    for (size_t i = 0; i < job->num_nodes; i++) {
        for (size_t j = 0; j < NODAL_DOF; j++) {
            job->u_dirichlet_mask[NODAL_DOF * i + j] = 0;
        }
    }

    /* Floor and ceiling. */
    for (size_t n = left_col; n <= right_col; n++) {
        size_t b = n + bottom_row * job->N;
        size_t t = n + top_row * job->N;

        /* bottom */
        if (bottom_fric & XFRICTION) {
            job->u_dirichlet[NODAL_DOF * b + XDOF_IDX] = 0;
            job->u_dirichlet_mask[NODAL_DOF * b + XDOF_IDX] = 1;
        }
        if (bottom_fric & YFRICTION) {
            job->u_dirichlet[NODAL_DOF * b + YDOF_IDX] = 0;
            job->u_dirichlet_mask[NODAL_DOF * b + YDOF_IDX] = 1;
        }

        /* top */
        if (top_fric & XFRICTION) {
            job->u_dirichlet[NODAL_DOF * t + XDOF_IDX] = 0;
            job->u_dirichlet_mask[NODAL_DOF * t + XDOF_IDX] = 1;
        }
        if (top_fric & YFRICTION) {
            job->u_dirichlet[NODAL_DOF * t + YDOF_IDX] = 0;
            job->u_dirichlet_mask[NODAL_DOF * t + YDOF_IDX] = 1;
        }
    }

    /* Side walls. */
    for (size_t n = bottom_row; n <= top_row; n++) {
        size_t l = n*job->N + left_col;
        size_t r = n*job->N + right_col;

        /* left */
        if (left_fric & XFRICTION) {
            job->u_dirichlet[NODAL_DOF * l + XDOF_IDX] = 0;
            job->u_dirichlet_mask[NODAL_DOF * l + XDOF_IDX] = 1;
        }
        if (left_fric & YFRICTION) {
            job->u_dirichlet[NODAL_DOF * l + YDOF_IDX] = 0;
            job->u_dirichlet_mask[NODAL_DOF * l + YDOF_IDX] = 1;
        }

        /* right */
        if (right_fric & XFRICTION) {
            job->u_dirichlet[NODAL_DOF * r + XDOF_IDX] = 0;
            job->u_dirichlet_mask[NODAL_DOF * r + XDOF_IDX] = 1;
        }
        if (right_fric & YFRICTION) {
            job->u_dirichlet[NODAL_DOF * r + YDOF_IDX] = 0;
            job->u_dirichlet_mask[NODAL_DOF * r + YDOF_IDX] = 1;
        }
    }

    return;
}
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
void generate_node_number_override(job_t *job)
{
    for (size_t i = 0; i < job->num_nodes; i++) {
        for (size_t j = 0; j < NODAL_DOF; j++) {
            job->node_number_override[NODAL_DOF * i + j] = (NODAL_DOF * i + j);
        }
    }

    return;
}
/*----------------------------------------------------------------------------*/
/* Only zeros out entries for now... */
void bc_momentum(job_t *job)
{
    for (size_t i = 0; i < job->num_nodes; i++) {
        for (size_t j = 0; j < NODAL_DOF; j++) {
            size_t n = NODAL_DOF * i + j;
            size_t m = job->node_number_override[n];
            if (job->u_dirichlet_mask[m] != 0) {
                /* only handle 0 displacement right now. */
                if (job->u_dirichlet[m] == 0) {
                    if (j == XDOF_IDX) {
                        job->nodes[i].mx_t = 0;
                    } else if (j == YDOF_IDX) {
                        job->nodes[i].my_t = 0;
                    }
                }
            }
        }
    }
    return;
}

/* Only zero out entries for now... */
void bc_force(job_t *job)
{
    for (size_t i = 0; i < job->num_nodes; i++) {
        for (size_t j = 0; j < NODAL_DOF; j++) {
            size_t n = NODAL_DOF * i + j;
            size_t m = job->node_number_override[n];
            if (job->u_dirichlet_mask[m] != 0) {
                /* only handle 0 displacement right now. */
                if (job->u_dirichlet[m] == 0) {
                    if (j == XDOF_IDX) {
                        job->nodes[i].fx = 0;
                    } else if (j == YDOF_IDX) {
                        job->nodes[i].fy = 0;
                    }
                }
            }
        }
    }
    return;
}

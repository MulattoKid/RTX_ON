/*
Copyright (c) 2018-2019 Daniel Fedai Larsen
*/

#ifndef DEFINES_H
#define DEFINES_H

/*
This file has all the defines that are shared across many shaders,
but also more importantly the locations of various shader resoruces.
These are used heavily in 'main.cpp', and makes adding/removing/changing
the descriptor set contents easier and less prone to mistakes.

WARNINIG: do not change these values unless you REALLY know what you're doing!
*/

// Ray generation payload locations
#define PRIMARY_PAYLOAD_LOCATION 0
#define SECONDARY_PAYLOAD_LOCATION 1

//////////////////////////
//FIRST RAY TRACING PASS//
//////////////////////////
// Shader locations
#define RT0_PRIMARY_CHIT_IDX 0
#define RT0_SECONDARY_CHIT_IDX 1
#define RT0_PRIMARY_MISS_IDX 0
#define RT0_SECONDARY_MISS_IDX 1

// Descriptor set locations
// Set 0
#define RT0_DESCRIPTOR_SET_0_NUM_BINDINGS 7
#define RT0_ACCELERATION_STRUCTURE_NV_BINDING_LOCATION 0
#define RT0_COLOR_IMAGE_BINDING_LOCATION 1
#define RT0_POSITION_IMAGE_BINDING_LOCATION 2
#define RT0_NORMAL_IMAGE_BINDING_LOCATION 3
#define RT0_CAMERA_BUFFER_BINDING_LOCATION 4
#define RT0_LIGHTS_BUFFER_BINDING_LOCATION 5
#define RT0_OTHER_DATA_BUFFER_BINDING_LOCATION 6

// Set 1
#define RT0_DESCRIPTOR_SET_1_NUM_BINDINGS 3
#define RT0_CUSTOM_ID_TO_ATTRIBUTE_ARRAY_INDEX_BUFFER_BINDING_LOCATION 0
#define RT0_PER_MESH_ATTRIBUTES_BINDING_LOCATION 1
#define RT0_PER_VERTEX_ATTRIBUTES_BINDING_LOCATION 2

///////////////////////////
//SECOND RAY TRACING PASS//
///////////////////////////
// Shader locations
#define RT1_PRIMARY_CHIT_IDX 0
#define RT1_PRIMARY_MISS_IDX 0

// Descriptor set locations
#define RT1_DESCRIPTOR_SET_NUM_BINDINGS 6
#define RT1_ACCELERATION_STRUCTURE_NV_BINDING_LOCATION 0
#define RT1_POSITION_IMAGE_BINDING_LOCATION 1
#define RT1_NORMAL_IMAGE_BINDING_LOCATION 2
#define RT1_AO_IMAGE_BINDING_LOCATION 3
#define RT1_CURRENT_FRAME_BINDING_LOCATION 4
#define RT1_BLUE_NOISE_IMAGE_BINDING_LOCATION 5

//////////////////////////
////RASTERIZATION PASS////
//////////////////////////
// Descriptor set location SUBPASS 0
#define RS0_DESCRIPTOR_SET_NUM_BINDINGS 3
#define RS0_RAY_TRACING_IMAGE_BINDING_LOCATION 0
#define RS0_AO_IMAGE_BINDING_LOCATION 1
#define RS0_BLUR_VARIABLE_BINDING_LOCATION 2

// Descriptor set location SUBPASS 1
#define RS1_DESCRIPTOR_SET_NUM_BINDINGS 4
#define RS1_PREVIOUS_FRAME_IMAGE_BINDING_LOCATION 0
#define RS1_POSITION_IMAGE_BINDING_LOCATION 1
#define RS1_CURRENT_FRAME_IMAGE_BINDING_LOCATION 2
#define RS1_CAMERA_BUFFER_BINDING_LOCATION 3

//////////////////////////
//MATHEMATICAL CONSTANTS//
//////////////////////////
#define PI 3.1415926535897932f
#define ONE_OVER_PI 0.3183098861837907f
#define TWO_PI 6.2831853071795865f
#define ONE_OVER_TWO_PI 0.1591549430918953f
#define GOLDEN_RATIO 1.61803398875f
#define GOLDEN_ANGLE 2.3999632297286533f

#endif

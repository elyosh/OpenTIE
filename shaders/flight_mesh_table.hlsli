#ifndef TIE_FLIGHT_MESH_TABLE_H
#define TIE_FLIGHT_MESH_TABLE_H

#include "../aeron/shaders/mesh_table_layout.hlsli"

StructuredBuffer<float4> mesh_tables : register(t0, space0);

uint flight_mesh_table_base(uint table_index)
{
    return table_index * AERON_MESH_TABLE_STRIDE_VEC4;
}

float4 flight_mesh_table_row(uint table_index, uint mesh_index, uint row)
{
    return mesh_tables[flight_mesh_table_base(table_index) +
                       mesh_index * 3 + row];
}

float flight_mesh_table_scalar(uint table_index, uint offset,
                               uint mesh_index)
{
    float4 packed = mesh_tables[flight_mesh_table_base(table_index) + offset +
                                (mesh_index >> 2)];
    return packed[mesh_index & 3];
}

#endif

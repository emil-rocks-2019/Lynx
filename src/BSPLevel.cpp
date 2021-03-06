#include "lynx.h"
#include <stdio.h>
#include "BSPLevel.h"
#include <SDL/SDL.h>
#include <GL/glew.h>
#define NO_SDL_GLEXT
#include <SDL/SDL_opengl.h>
#include <memory>
#include "Renderer.h"

#define BSP_EPSILON         (0.1f)

#ifdef _DEBUG
#include <crtdbg.h>
#define new new(_NORMAL_BLOCK,__FILE__, __LINE__)
#endif

CBSPLevel::CBSPLevel(void)
{
    m_uselightmap = false;
    m_lightmap = 0;
    m_plane = NULL;
    m_tex = NULL;
    m_texid = NULL;
    m_node = NULL;
    m_leaf = NULL;
    m_triangle = NULL;
    m_vertex = NULL;
    m_indices = NULL;
    m_spawnpoint = NULL;

    m_planecount = 0;
    m_texcount = 0;
    m_nodecount = 0;
    m_leafcount = 0;
    m_trianglecount = 0;
    m_vertexcount = 0;
    m_spawnpointcount = 0;

    m_vbo = 0;
    m_vboindex = 0;
}

CBSPLevel::~CBSPLevel(void)
{
    Unload();
}

bool CBSPLevel::Load(std::string file, CResourceManager* resman)
{
    static const vec3_t axis[3] = {vec3_t::xAxis,
                                   vec3_t::yAxis,
                                   vec3_t::zAxis};
    uint32_t i, j, k;
    int errcode;
    FILE* f = fopen(file.c_str(), "rb");
    if(!f)
        return false;

    fprintf(stderr, "Loading level: %s\n", file.c_str());

    Unload();
    // This code has three sections:
    // Reading the header
    // Reading the file allocation table
    // Reading the actual data

    // Reading header
    bspbin_header_t header;

    fread(&header, sizeof(header), 1, f);
    if(header.magic != BSPBIN_MAGIC)
    {
        fprintf(stderr, "BSP: Not a valid Lynx BSP file\n");
        return false;
    }
    if(header.version != BSPBIN_VERSION)
    {
        fprintf(stderr, "BSP: Wrong Lynx BSP file format version. Expecting: %i, got: %i\n", BSPBIN_VERSION, header.version);
        return false;
    }
    if(header.lightmap)
    {
        m_uselightmap = true;

        if(resman)
        {
            std::string lightmappath = CLynx::GetDirectory(file) + "lightmap.jpg";
            m_lightmap = resman->GetTexture(lightmappath);
            assert(m_lightmap);
            if(m_lightmap == 0)
            {
                fprintf(stderr, "No lightmap for level found: %s\n", lightmappath.c_str());
                return false;
            }
        }
    }
    else
    {
        m_uselightmap = false;
        fprintf(stderr, "Information: No lightmap in lbsp file\n");
    }

    // Reading file allocation table
    bspbin_direntry_t dirplane;
    bspbin_direntry_t dirtextures;
    bspbin_direntry_t dirnodes;
    bspbin_direntry_t dirtriangles;
    bspbin_direntry_t dirvertices;
    bspbin_direntry_t dirspawnpoints;
    bspbin_direntry_t dirleafs;

    fread(&dirplane, sizeof(dirplane), 1, f);
    fread(&dirtextures, sizeof(dirtextures), 1, f);
    fread(&dirnodes, sizeof(dirnodes), 1, f);
    fread(&dirtriangles, sizeof(dirtriangles), 1, f);
    fread(&dirvertices, sizeof(dirvertices), 1, f);
    fread(&dirspawnpoints, sizeof(dirspawnpoints), 1, f);
    fread(&dirleafs, sizeof(dirleafs), 1, f);
    if(ftell(f) != BSPBIN_HEADER_LEN)
    {
        fprintf(stderr, "BSP: Error reading header\n");
        return false;
    }

    m_planecount = dirplane.length / sizeof(bspbin_plane_t);
    m_texcount = dirtextures.length / sizeof(bspbin_texture_t);
    m_nodecount = dirnodes.length / sizeof(bspbin_node_t);
    m_trianglecount = dirtriangles.length / sizeof(bspbin_triangle_t);
    m_vertexcount = dirvertices.length / sizeof(bspbin_vertex_t);
    m_spawnpointcount = dirspawnpoints.length / sizeof(bspbin_spawn_t);
    m_leafcount = dirleafs.length; // special case for the leafs

    m_plane = new plane_t[ m_planecount ];
    m_tex = new bspbin_texture_t[ m_texcount ];
    m_texid = new int[ m_texcount ];
    m_node = new bspbin_node_t[ m_nodecount ];
    m_triangle = new bspbin_triangle_t[ m_trianglecount ];
    m_vertex = new bspbin_vertex_t[ m_vertexcount ];
    m_spawnpoint = new bspbin_spawn_t[ m_spawnpointcount ];
    m_leaf = new bspbin_leaf_t[ m_leafcount ];

    if(!m_plane ||
       !m_tex ||
       !m_texid ||
       !m_node ||
       !m_leaf ||
       !m_triangle ||
       !m_vertex ||
       !m_spawnpoint)
    {
        Unload();
        fprintf(stderr, "BSP: Not enough memory to load Lynx BSP\n");
        return false;
    }

    // Reading data

    bspbin_plane_t kdplane;
    fseek(f, dirplane.offset, SEEK_SET);
    for(i=0; i<m_planecount; i++) // reconstruct the planes
    {
        fread(&kdplane, sizeof(kdplane), 1, f);
        if(kdplane.type > 2)
        {
            assert(0); // let me see this
            Unload();
            fprintf(stderr, "BSP: Unknown plane type\n");
            return false;
        }
        m_plane[i].m_d = kdplane.d;
        m_plane[i].m_n = axis[kdplane.type]; // lookup table
    }

    fseek(f, dirtextures.offset, SEEK_SET);
    fread(m_tex, sizeof(bspbin_texture_t), m_texcount, f);

    fseek(f, dirnodes.offset, SEEK_SET);
    fread(m_node, sizeof(bspbin_node_t), m_nodecount, f);

    fseek(f, dirtriangles.offset, SEEK_SET);
    fread(m_triangle, sizeof(bspbin_triangle_t), m_trianglecount, f);

    fseek(f, dirvertices.offset, SEEK_SET);
    fread(m_vertex, sizeof(bspbin_vertex_t), m_vertexcount, f);

    fseek(f, dirspawnpoints.offset, SEEK_SET);
    fread(m_spawnpoint, sizeof(bspbin_spawn_t), m_spawnpointcount, f);

    // Reading the triangle indices of the leafs
    fseek(f, dirleafs.offset, SEEK_SET);

    uint32_t trianglecount, triangleindex;
    for(i = 0; i < dirleafs.length; i++)
    {
        fread(&trianglecount, sizeof(trianglecount), 1, f);
        m_leaf[i].triangles.reserve(trianglecount);
        for(j=0; j<trianglecount; j++)
        {
            fread(&triangleindex, sizeof(triangleindex), 1, f);
            m_leaf[i].triangles.push_back(triangleindex);
        }
    }

    // integrity check, the last 4 bytes in a lbsp version >=5 file
    // is the magic pattern.
    uint32_t endmark;
    fread(&endmark, sizeof(endmark), 1, f);
    if(endmark != BSPBIN_MAGIC)
    {
        Unload();
        fprintf(stderr, "BSP: Error reading lumps from BSP file\n");
        return false;
    }

    // Check if tree file indices are within a valid range
    for(i=0;i<m_nodecount;i++)
    {
        for(k=0;k<2;k++)
        {
            if(m_node[i].children[k] < 0) // leaf index
            {
                if(-m_node[i].children[k]-1 >= (int)m_leafcount)
                {
                    fprintf(stderr, "BSP: Invalid leaf pointer\n");
                    Unload();
                    return false;
                }
            }
            else
            {
                if(m_node[i].children[k] >= (int)m_nodecount)
                {
                    fprintf(stderr, "BSP: Invalid node pointer\n");
                    Unload();
                    return false;
                }
            }
        }
    }

    m_filename = file;

    // Rendering stuff, if we are running as a server,
    // we can return now.
    if(!resman)
    {
        m_vbo = 0;
        m_vboindex = 0;
        return true;
    }

    // Setup indices
    const uint32_t indexcount = m_trianglecount*3;
    m_indices = new vertexindex_t[ indexcount ];
    if(!m_indices)
    {
        Unload();
        fprintf(stderr, "BSP: Not enough memory for index buffer array\n");
        return false;
    }

    // we use this normal map, if no texture_bump.jpg is available
    int texnormal_fallback = resman->GetTexture(CLynx::GetBaseDirTexture() + "normal.jpg");
    if(texnormal_fallback == 0)
    {
        fprintf(stderr, "Failed to load standard normal map");
        return false;
    }

    uint32_t vertexindex = 0;
    for(i=0;i<m_texcount;i++)
    {
        // loading all textures
        const std::string texpath = CLynx::GetDirectory(file) + m_tex[i].name;

        const std::string texpathnoext = CLynx::StripFileExtension(m_tex[i].name);
        const std::string texpathext = CLynx::GetFileExtension(m_tex[i].name);
        const std::string texpathnormal = CLynx::GetDirectory(file) + texpathnoext + "_bump" + texpathext;

        // we group all triangles with the same
        // texture in a complete batch of vertex indices
        bsp_texture_batch_t thisbatch;

        thisbatch.start = vertexindex;

        m_texid[i] = resman->GetTexture(texpath);
        thisbatch.texid = m_texid[i];
        thisbatch.texidnormal = resman->GetTexture(texpathnormal, true); // we only try to load the bump map
        if(thisbatch.texidnormal == 0) // no texture found, use fall back texture
            thisbatch.texidnormal = texnormal_fallback;

        for(j=0;j<m_trianglecount;j++)
        {
            if(m_triangle[j].tex == i) // this triangle is using the current texture
            {
                m_indices[vertexindex++] = m_triangle[j].v[0];
                m_indices[vertexindex++] = m_triangle[j].v[1];
                m_indices[vertexindex++] = m_triangle[j].v[2];
            }
        }
        thisbatch.count = vertexindex - thisbatch.start;
        m_texturebatch.push_back(thisbatch);
    }
    assert(indexcount == vertexindex);

    glGenBuffers(1, &m_vbo);
    if(m_vbo < 1)
    {
        Unload();
        fprintf(stderr, "BSP: Failed to generate VBO\n");
        return false;
    }

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    errcode = glGetError();
    if(errcode != GL_NO_ERROR)
    {
        fprintf(stderr, "BSP: Failed to bind VBO: %i\n", errcode);
        assert(0);
        Unload();
        return false;
    }

    glBufferData(GL_ARRAY_BUFFER, sizeof(bspbin_vertex_t) * m_vertexcount, NULL, GL_STATIC_DRAW);
    if(glGetError() != GL_NO_ERROR)
    {
        Unload();
        fprintf(stderr, "BSP: Failed to buffer data\n");
        return false;
    }

    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(bspbin_vertex_t) * m_vertexcount, m_vertex);
    if(glGetError() != GL_NO_ERROR)
    {
        Unload();
        fprintf(stderr, "BSP: Failed to upload VBO data\n");
        return false;
    }

    // the following depends on the bspbin_vertex_t struct
    // (byte offsets)
    glVertexPointer(3, GL_FLOAT, sizeof(bspbin_vertex_t), BUFFER_OFFSET(0));
    glNormalPointer(GL_FLOAT, sizeof(bspbin_vertex_t), BUFFER_OFFSET(12));
    glClientActiveTexture(GL_TEXTURE0);
    glTexCoordPointer(2, GL_FLOAT, sizeof(bspbin_vertex_t), BUFFER_OFFSET(24));
    glClientActiveTexture(GL_TEXTURE1);
    glTexCoordPointer(4, GL_FLOAT, sizeof(bspbin_vertex_t), BUFFER_OFFSET(32));

    if(glGetError() != GL_NO_ERROR)
    {
        Unload();
        fprintf(stderr, "BSP: Failed to init VBO\n");
        return false;
    }

    glGenBuffers(1, &m_vboindex);
    if(m_vboindex < 1)
    {
        Unload();
        fprintf(stderr, "BSP: Failed to generate index VBO\n");
        return false;
    }

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_vboindex);
    if(glGetError() != GL_NO_ERROR)
    {
        Unload();
        fprintf(stderr, "BSP: Failed to bind index VBO\n");
        return false;
    }

    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(vertexindex_t) * indexcount, NULL, GL_STATIC_DRAW);
    if(glGetError() != GL_NO_ERROR)
    {
        Unload();
        fprintf(stderr, "BSP: Failed to buffer index data\n");
        return false;
    }

    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, sizeof(vertexindex_t) * indexcount, m_indices);
    if(glGetError() != GL_NO_ERROR)
    {
        Unload();
        fprintf(stderr, "BSP: Failed to upload index VBO data\n");
        return false;
    }

    return true;
}

void CBSPLevel::Unload()
{
    m_uselightmap = false;
    m_lightmap = 0;

    SAFE_RELEASE_ARRAY(m_plane);
    SAFE_RELEASE_ARRAY(m_tex);
    SAFE_RELEASE_ARRAY(m_texid);
    SAFE_RELEASE_ARRAY(m_node);
    SAFE_RELEASE_ARRAY(m_leaf);
    SAFE_RELEASE_ARRAY(m_triangle);
    SAFE_RELEASE_ARRAY(m_vertex);
    SAFE_RELEASE_ARRAY(m_indices);
    SAFE_RELEASE_ARRAY(m_spawnpoint);

    m_texturebatch.clear();

    m_filename = "";

    m_planecount = 0;
    m_texcount = 0;
    m_nodecount = 0;
    m_leafcount = 0;
    m_trianglecount = 0;
    m_vertexcount = 0;
    m_spawnpointcount = 0;

    if(m_vbo > 0)
        glDeleteBuffers(1, &m_vbo);
    if(m_vboindex > 0)
        glDeleteBuffers(1, &m_vboindex);

    m_vbo = 0;
    m_vboindex = 0;
}

static const bspbin_spawn_t s_spawn_default;
bspbin_spawn_t CBSPLevel::GetRandomSpawnPoint() const
{
    if(m_spawnpointcount < 1)
        return s_spawn_default;
    return m_spawnpoint[rand()%m_spawnpointcount];
}

void CBSPLevel::RenderGL(const vec3_t& origin, const CFrustum& frustum) const
{
    if(!IsLoaded())
        return;

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_vboindex);

    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, sizeof(bspbin_vertex_t), BUFFER_OFFSET(0));

    glEnableClientState(GL_NORMAL_ARRAY);
    glNormalPointer(GL_FLOAT, sizeof(bspbin_vertex_t), BUFFER_OFFSET(12));

    glClientActiveTexture(GL_TEXTURE0);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(2, GL_FLOAT, sizeof(bspbin_vertex_t), BUFFER_OFFSET(24));

    glClientActiveTexture(GL_TEXTURE1);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(4, GL_FLOAT, sizeof(bspbin_vertex_t), BUFFER_OFFSET(32));

    if(m_uselightmap)
    {
        glClientActiveTexture(GL_TEXTURE2);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, sizeof(bspbin_vertex_t), BUFFER_OFFSET(32+16));

        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, m_lightmap);
    }

    const uint32_t batchcount = m_texturebatch.size();
    for(uint32_t i=0; i<batchcount; i++)
    {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_texturebatch[i].texidnormal);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_texturebatch[i].texid);

        glDrawElements(GL_TRIANGLES,
                       m_texturebatch[i].count,
                       MY_GL_VERTEXINDEX_TYPE,
                       BUFFER_OFFSET(m_texturebatch[i].start * sizeof(vertexindex_t)));
    }

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);

    glClientActiveTexture(GL_TEXTURE1);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisable(GL_TEXTURE_2D);

    if(m_uselightmap)
    {
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, 0);

        glClientActiveTexture(GL_TEXTURE2);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    }

    glActiveTexture(GL_TEXTURE0);
    glClientActiveTexture(GL_TEXTURE0);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);

    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void CBSPLevel::RenderNormals() const
{
    unsigned int vindex;
    const vec3_t tanoff(0.06f); // offset for tangents
    const vec3_t bitanoff(-0.06f); // offset for tangents
    const float nscale = 0.3f;
    vec3_t bitangent;

    glBegin(GL_LINES);
    for(vindex = 0; vindex < m_vertexcount; vindex++)
    {
        glColor4f(1.0f, 0.0f, 0.0f, 1.0f);
        glVertex3fv(m_vertex[vindex].v.GetPointer());
        glVertex3fv((nscale*m_vertex[vindex].n + m_vertex[vindex].v).GetPointer());

        glColor3f(0.0f, 1.0f, 0.0f);
        glVertex3fv((tanoff + m_vertex[vindex].v).GetPointer());
        glVertex3fv((nscale*m_vertex[vindex].t + m_vertex[vindex].v + tanoff).GetPointer());

        glColor3f(0.0f, 0.0f, 1.0f);
        bitangent = m_vertex[vindex].w * (m_vertex[vindex].n ^ m_vertex[vindex].t);
        glVertex3fv((bitanoff + m_vertex[vindex].v).GetPointer());
        glVertex3fv((nscale*bitangent + m_vertex[vindex].v + bitanoff).GetPointer());
    }
    glEnd();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

// Static test
bool CBSPLevel::SphereTriangleIntersectStatic(const int triangleindex,
                                              const vec3_t& sphere_pos,
                                              const float sphere_radius) const
{
    const unsigned int vertexindex1 = m_triangle[triangleindex].v[0];
    const unsigned int vertexindex2 = m_triangle[triangleindex].v[1];
    const unsigned int vertexindex3 = m_triangle[triangleindex].v[2];
    const vec3_t& OA = m_vertex[vertexindex1].v;
    const vec3_t& OB = m_vertex[vertexindex2].v;
    const vec3_t& OC = m_vertex[vertexindex3].v;
    const vec3_t& P = sphere_pos;
    const float r = sphere_radius;

    // Copy from: http://realtimecollisiondetection.net/blog/?p=103

    const vec3_t A = OA - P;
    const vec3_t B = OB - P;
    const vec3_t C = OC - P;
    const float rr = r * r;
    const vec3_t V = vec3_t::cross(B - A, C - A);
    const float d = vec3_t::dot(A, V);
    const float e = vec3_t::dot(V, V);
    const bool sep1 = d * d > rr * e;
    const float aa = vec3_t::dot(A, A);
    const float ab = vec3_t::dot(A, B);
    const float ac = vec3_t::dot(A, C);
    const float bb = vec3_t::dot(B, B);
    const float bc = vec3_t::dot(B, C);
    const float cc = vec3_t::dot(C, C);
    const bool sep2 = (aa > rr) && (ab > aa) && (ac > aa);
    const bool sep3 = (bb > rr) && (ab > bb) && (bc > bb);
    const bool sep4 = (cc > rr) && (ac > cc) && (bc > cc);
    const vec3_t AB = B - A;
    const vec3_t BC = C - B;
    const vec3_t CA = A - C;
    const float d1 = ab - aa;
    const float d2 = bc - bb;
    const float d3 = ac - cc;
    const float e1 = vec3_t::dot(AB, AB);
    const float e2 = vec3_t::dot(BC, BC);
    const float e3 = vec3_t::dot(CA, CA);
    const vec3_t Q1 = A * e1 - d1 * AB;
    const vec3_t Q2 = B * e2 - d2 * BC;
    const vec3_t Q3 = C * e3 - d3 * CA;
    const vec3_t QC = C * e1 - Q1;
    const vec3_t QA = A * e2 - Q2;
    const vec3_t QB = B * e3 - Q3;
    const bool sep5 = (vec3_t::dot(Q1, Q1) > rr * e1 * e1) && (vec3_t::dot(Q1, QC) > 0);
    const bool sep6 = (vec3_t::dot(Q2, Q2) > rr * e2 * e2) && (vec3_t::dot(Q2, QA) > 0);
    const bool sep7 = (vec3_t::dot(Q3, Q3) > rr * e3 * e3) && (vec3_t::dot(Q3, QB) > 0);
    const bool separated = sep1 | sep2 | sep3 | sep4 | sep5 | sep6 | sep7;

    return !separated;
}

// This method checks the movement of a sphere along a path,
// given by the position sphere_pos and the dir vector.
// If there is a collision with the triangle, the f vector
// indicates the fraction along the dir vector to the hit point.
//
// returns true, if the triangle is hit.
//
// Using methods from Eric Lengyel's math book.
bool CBSPLevel::SphereTriangleIntersect(const int triangleindex,
                                        const vec3_t& sphere_pos,
                                        const float sphere_radius,
                                        const vec3_t& dir,
                                        float* f,
                                        vec3_t* hitnormal,
                                        vec3_t* hitpoint) const
{
    float cf; // fraction of travelling along the dir vector
    int i;

    const int vertexindex1 = m_triangle[triangleindex].v[0];
    const int vertexindex2 = m_triangle[triangleindex].v[1];
    const int vertexindex3 = m_triangle[triangleindex].v[2];
    const vec3_t& P0 = m_vertex[vertexindex1].v;
    const vec3_t& P1 = m_vertex[vertexindex2].v;
    const vec3_t& P2 = m_vertex[vertexindex3].v;
    plane_t plane(P0, P1, P2); // triangle plane
    plane.m_d -= sphere_radius; // plane shift

    *f = MAX_TRACE_DIST;

    // Step 1) triangle face
    const bool not_parallel = plane.GetIntersection(&cf, sphere_pos, dir);
    if(not_parallel && cf >= 0.0f)
    {
        // calculate the hitpoint
        const vec3_t tmp_hitpoint = sphere_pos + dir*cf - plane.m_n*sphere_radius;

        // check if we are inside the triangle
        // Barycentric coordinates (math for 3d game programming p. 144)
        const vec3_t R = tmp_hitpoint - P0;
        const vec3_t Q1 = P1 - P0;
        const vec3_t Q2 = P2 - P0;
        const float Q1Q2 = Q1*Q2;
        const float Q1_sqr = Q1.AbsSquared();
        const float Q2_sqr = Q2.AbsSquared();
        const float invdet = 1/(Q1_sqr*Q2_sqr - Q1Q2*Q1Q2);
        const float RQ1 = R * Q1;
        const float RQ2 = R * Q2;
        const float w1 = invdet*(Q2_sqr*RQ1 - Q1Q2*RQ2);
        const float w2 = invdet*(-Q1Q2*RQ1  + Q1_sqr*RQ2);

        if(w1 >= 0 && w2 >= 0 && (w1 + w2 <= 1))
        {
            *f = cf;
            *hitnormal = plane.m_n;
            *hitpoint = tmp_hitpoint;
            return true; // skip edge and vertex, it must take place before them
        }
    }

    // Step 2) Edge detection
    for(i=0;i<3;i++) // for every edge of triangle
    {
        const vec3_t& fromvec = m_vertex[m_triangle[triangleindex].v[i]].v;
        const int nextindex = (i+1)%3; // keep vertex index between 0 and 2
        const vec3_t& tovec = m_vertex[m_triangle[triangleindex].v[nextindex]].v;
        if(!vec3_t::RayCylinderIntersect(sphere_pos,
                                         dir,
                                         fromvec,
                                         tovec,
                                         sphere_radius,
                                         &cf))
        {
            continue;
        }

        if(cf < *f && cf >= 0.0f)
        {
            *f = cf;

            *hitpoint = sphere_pos + dir*cf;
            // calculate normal
            const vec3_t tmpvec = vec3_t::cross(fromvec-(*hitpoint), tovec-(*hitpoint));
            *hitnormal = (vec3_t::cross(tmpvec, tovec-fromvec)).Normalized();
        }
    }

    // Step 3) Vertex detection
    for(i=0;i<3;i++) // for every vertex of triangle
    {
        const vec3_t& v = m_vertex[m_triangle[triangleindex].v[i]].v;
        if(!vec3_t::RaySphereIntersect(sphere_pos,
                                       dir,
                                       v,
                                       sphere_radius,
                                       &cf))
        {
            continue;
        }

        if(cf < *f && cf >= 0.0f)
        {
            *f = cf;

            *hitpoint = sphere_pos + dir*cf;
            *hitnormal = *hitpoint - v;
            hitnormal->Normalize();
        }
    }

    return *f != MAX_TRACE_DIST;
}

#define DIST_EPSILON    (0.02f)  // 2 cm epsilon for triangle collision
#define MIN_FRACTION    (0.005f) // at least 0.5% movement along the direction vector

// checking the movement of a sphere along a given path
void CBSPLevel::TraceSphere(bsp_sphere_trace_t* trace) const
{
    if(m_node == NULL)
    {
        fprintf(stderr, "Warning: Tracing in unloaded level\n");
        trace->f = MAX_TRACE_DIST;
        return;
    }
    trace->f = MAX_TRACE_DIST;
    TraceSphere(trace, 0);
    assert(trace->f >= 0.0f);
}

void CBSPLevel::TraceSphere(bsp_sphere_trace_t* trace, const int node) const
{
    if(node < 0) // have we reached a leaf (leafs have a negative index)?
    {
        int triangleindex;
        const int leafindex = -node-1; // convert negative node index to leaf index
        const unsigned int trianglecount = m_leaf[leafindex].triangles.size();
        float cf;
        float minf = trace->f;
        plane_t hitplane;
        vec3_t hitnormal;
        vec3_t hitpoint;
        vec3_t normal;
        // check every triangle in the leaf
        for(unsigned int i=0;i<trianglecount;i++)
        {
            triangleindex = m_leaf[leafindex].triangles[i];
            // check if we hit this triangle
            if(SphereTriangleIntersect(triangleindex,
                                       trace->start,
                                       trace->radius,
                                       trace->dir,
                                       &cf,
                                       &hitnormal,
                                       &hitpoint))
            {
                // collision with triangle found:
                //
                // safety shift along the trace path.  this keeps the sphere
                // DIST_EPSILON away from the plane along the plane normal.
                // this line is super important for a stable collision
                // detection.
                cf += DIST_EPSILON/(hitnormal * trace->dir);

                if(cf < MIN_FRACTION)
                    cf = 0.0f; // prevent small movements

                if(cf < minf)
                {
                    hitplane.SetupPlane(hitpoint, hitnormal);
                    trace->p = hitplane;
                    trace->f = cf;
                    minf = cf;
                }
            }
        }
        return;
    }

    pointplane_t locstart;
    pointplane_t locend;

    // Check if everything is in front of the split plane
    plane_t tmpplane = m_plane[m_node[node].plane];
    tmpplane.m_d -= trace->radius;
    locstart = tmpplane.Classify(trace->start, BSP_EPSILON);
    locend = tmpplane.Classify(trace->start + trace->dir, BSP_EPSILON);
    if(locstart > POINT_ON_PLANE && locend > POINT_ON_PLANE)
    {
        TraceSphere(trace, m_node[node].children[0]);
        return;
    }

    // Check if everything is behind the split plane
    tmpplane.m_d = m_plane[m_node[node].plane].m_d + trace->radius;
    locstart = tmpplane.Classify(trace->start, BSP_EPSILON);
    locend = tmpplane.Classify(trace->start + trace->dir, BSP_EPSILON);
    if(locstart < POINT_ON_PLANE && locend < POINT_ON_PLANE)
    {
        TraceSphere(trace, m_node[node].children[1]);
        return;
    }

    bsp_sphere_trace_t trace1 = *trace;
    bsp_sphere_trace_t trace2 = *trace;

    TraceSphere(&trace1, m_node[node].children[0]);
    TraceSphere(&trace2, m_node[node].children[1]);

    if(trace1.f < trace2.f)
        *trace = trace1;
    else
        *trace = trace2;
}

bool CBSPLevel::IsSphereStuck(const vec3_t& position, const float radius) const
{
    if(m_node == NULL)
        return false;

    return IsSphereStuck(position, radius, 0); // 0 = root node
}

bool CBSPLevel::IsSphereStuck(const vec3_t& position, const float radius, const int node) const
{
    if(node < 0) // have we reached a leaf?
    {
        int triangleindex;
        const int leafindex = -node-1;
        const unsigned int trianglecount = m_leaf[leafindex].triangles.size();

        for(unsigned int i=0;i<trianglecount;i++)
        {
            triangleindex = m_leaf[leafindex].triangles[i];
            if(SphereTriangleIntersectStatic(triangleindex, position, radius))
                return true;
        }
        return false;
    }

    pointplane_t loc;

    // Check if everything is in front of the plane
    plane_t tmpplane = m_plane[m_node[node].plane];
    tmpplane.m_d -= radius;
    loc = tmpplane.Classify(position, BSP_EPSILON);
    if(loc == POINTPLANE_FRONT)
    {
        return IsSphereStuck(position, radius, m_node[node].children[0]);
    }

    // Check if everythinig is behind the plane
    tmpplane.m_d = m_plane[m_node[node].plane].m_d + radius;
    loc = tmpplane.Classify(position, BSP_EPSILON);
    if(loc == POINTPLANE_BACK)
    {
        return IsSphereStuck(position, radius, m_node[node].children[1]);
    }

    // Check both
    if(IsSphereStuck(position, radius, m_node[node].children[0]))
        return true;
    return IsSphereStuck(position, radius, m_node[node].children[1]);
}


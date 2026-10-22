/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// tr_marks.c -- polygon projection on the world polygons

#include "tr_local.h"
//#include "assert.h"
#define MAX_VERTS_ON_POLY		64

#define MARKER_OFFSET			0	// 1

/*
=============
R_ChopPolyBehindPlane

Out must have space for two more vertexes than in
=============
*/
#define	SIDE_FRONT	0
#define	SIDE_BACK	1
#define	SIDE_ON		2
#ifdef REND2_SP
static void R_ChopPolyBehindPlane(const int num_in_points, vec3_t in_points[MAX_VERTS_ON_POLY], int* num_out_points, vec3_t out_points[MAX_VERTS_ON_POLY], vec3_t normal, const vec_t dist, const vec_t epsilon)
#else // !REND2_SP

static void R_ChopPolyBehindPlane(const int numInPoints, vec3_t inPoints[MAX_VERTS_ON_POLY], int* numOutPoints, vec3_t outPoints[MAX_VERTS_ON_POLY], vec3_t normal, const float dist, const float epsilon)
#endif // REND2_SP
{
	float		dists[MAX_VERTS_ON_POLY + 4] = { 0 };
	int			sides[MAX_VERTS_ON_POLY + 4] = { 0 };
#ifdef REND2_SP
	int			counts[3];
#else // !REND2_SP
	int			counts[3]{};
#endif // REND2_SP
	float		dot = 0.0f;
	int			i = 0, j = 0;
	float* p1 = nullptr, * p2 = nullptr, * clip = nullptr;
	float		d = 0.0f;

	// don't clip if it might overflow
#ifdef REND2_SP
	if (num_in_points >= MAX_VERTS_ON_POLY - 2) {
		*num_out_points = 0;
#else // !REND2_SP
	if (numInPoints >= MAX_VERTS_ON_POLY - 2) {
		*numOutPoints = 0;
#endif // REND2_SP
		return;
	}

	counts[0] = counts[1] = counts[2] = 0;

	// determine sides for each point
#ifdef REND2_SP
	for (i = 0; i < num_in_points; i++) {
		dot = DotProduct(in_points[i], normal);
#else // !REND2_SP
	for (i = 0; i < numInPoints; i++) {
		dot = DotProduct(inPoints[i], normal);
#endif // REND2_SP
		dot -= dist;
		dists[i] = dot;
		if (dot > epsilon) {
			sides[i] = SIDE_FRONT;
		}
		else if (dot < -epsilon) {
			sides[i] = SIDE_BACK;
		}
		else {
			sides[i] = SIDE_ON;
		}
		counts[sides[i]]++;
	}
	sides[i] = sides[0];
	dists[i] = dists[0];

#ifdef REND2_SP
	*num_out_points = 0;
#else // !REND2_SP
	*numOutPoints = 0;
#endif // REND2_SP

	if (!counts[0]) {
		return;
	}
	if (!counts[1]) {
#ifdef REND2_SP
		*num_out_points = num_in_points;
		Com_Memcpy(out_points, in_points, num_in_points * sizeof(vec3_t));
#else // !REND2_SP
		*numOutPoints = numInPoints;
		Com_Memcpy(outPoints, inPoints, numInPoints * sizeof(vec3_t));
#endif // REND2_SP
		return;
	}

#ifdef REND2_SP
	for (i = 0; i < num_in_points; i++) {
		p1 = in_points[i];
		clip = out_points[*num_out_points];
#else // !REND2_SP
	for (i = 0; i < numInPoints; i++) {
		p1 = inPoints[i];
		clip = outPoints[*numOutPoints];
#endif // REND2_SP

		if (sides[i] == SIDE_ON) {
			VectorCopy(p1, clip);
#ifdef REND2_SP
			(*num_out_points)++;
#else // !REND2_SP
			(*numOutPoints)++;
#endif // REND2_SP
			continue;
		}

		if (sides[i] == SIDE_FRONT) {
			VectorCopy(p1, clip);
#ifdef REND2_SP
			(*num_out_points)++;
			clip = out_points[*num_out_points];
#else // !REND2_SP
			(*numOutPoints)++;
			clip = outPoints[*numOutPoints];
#endif // REND2_SP
		}

		if (sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i]) {
			continue;
		}

		// generate a split point
#ifdef REND2_SP
		p2 = in_points[(i + 1) % num_in_points];
#else // !REND2_SP
		p2 = inPoints[(i + 1) % numInPoints];
#endif // REND2_SP

		d = dists[i] - dists[i + 1];
		if (d == 0) {
			dot = 0;
		}
		else {
			dot = dists[i] / d;
		}

		// clip xyz

		for (j = 0; j < 3; j++) {
			clip[j] = p1[j] + dot * (p2[j] - p1[j]);
		}

#ifdef REND2_SP
		(*num_out_points)++;
#else // !REND2_SP
		(*numOutPoints)++;
#endif // REND2_SP
	}
}

/*
=================
R_BoxSurfaces_r

=================
*/
#ifdef REND2_SP
void R_BoxSurfaces_r(const mnode_t* node, vec3_t mins, vec3_t maxs, surfaceType_t** list, const int listsize, int* listlength, vec3_t dir)
#else // !REND2_SP
static void R_BoxSurfaces_r(const mnode_t* node, vec3_t mins, vec3_t maxs, surfaceType_t** list, const int listsize, int* listlength, vec3_t dir)
#endif // REND2_SP
{
	int			s = 0, c = 0;
	msurface_t* surf = nullptr;
	int* mark = nullptr;

	// do the tail recursion in a loop
	while (node->contents == -1) {
		s = BoxOnPlaneSide(mins, maxs, node->plane);
		if (s == 1) {
			node = node->children[0];
		}
		else if (s == 2) {
			node = node->children[1];
		}
		else {
			R_BoxSurfaces_r(node->children[0], mins, maxs, list, listsize, listlength, dir);
			node = node->children[1];
		}
	}

	// add the individual surfaces
	mark = tr.world->marksurfaces + node->firstmarksurface;
	c = node->nummarksurfaces;
	while (c--) {
		int* surfViewCount = nullptr;
		//
		if (*listlength >= listsize) break;
		//
		surfViewCount = &tr.world->surfacesViewCount[*mark];
		surf = tr.world->surfaces + *mark;
		// check if the surface has NOIMPACT or NOMARKS set
		if ((surf->shader->surfaceFlags & (SURF_NOIMPACT | SURF_NOMARKS))
			|| (surf->shader->contentFlags & CONTENTS_FOG)) {
			*surfViewCount = tr.viewCount;
		}
		// extra check for surfaces to avoid list overflows
		else if (*(surf->data) == SF_FACE) {
			// the face plane should go through the box
			s = BoxOnPlaneSide(mins, maxs, &surf->cullinfo.plane);
			if (s == 1 || s == 2) {
				*surfViewCount = tr.viewCount;
			}
			else if (DotProduct(surf->cullinfo.plane.normal, dir) > -0.5) {
				// don't add faces that make sharp angles with the projection direction
				*surfViewCount = tr.viewCount;
			}
		}
		else if (*(surf->data) != SF_GRID &&
			*(surf->data) != SF_TRIANGLES)
			*surfViewCount = tr.viewCount;
		// check the viewCount because the surface may have
		// already been added if it spans multiple leafs
		if (*surfViewCount != tr.viewCount) {
			*surfViewCount = tr.viewCount;
			list[*listlength] = surf->data;
			(*listlength)++;
		}
		mark++;
	}
}

/*
=================
R_AddMarkFragments

=================
*/
#ifdef REND2_SP
void R_AddMarkFragments(int num_clip_points, vec3_t clip_points[2][MAX_VERTS_ON_POLY], const int numPlanes, vec3_t* normals, const float* dists, const int max_points, vec3_t point_buffer, int max_fragments, markFragment_t* fragment_buffer, int* returned_points, int* returned_fragments, vec3_t mins, vec3_t maxs)
#else // !REND2_SP
static void R_AddMarkFragments(int numClipPoints, vec3_t clipPoints[2][MAX_VERTS_ON_POLY], const int numPlanes, vec3_t* normals, const float* dists, const int maxPoints, vec3_t pointBuffer, int maxFragments, markFragment_t* fragmentBuffer, int* returnedPoints, int* returnedFragments, vec3_t mins, vec3_t maxs)
#endif // REND2_SP
{
	int pingPong = 0, i = 0;
	markFragment_t* mf = nullptr;

	// Skip polygons that are nowhere near the mark before chopping them by all planes:
#ifdef REND2_SP
	// big triangle meshes / grids would otherwise chop every triangle for every mark
	// (e.g. the blob shadow of every NPC, every frame). 32 = the near clip plane margin.
	for (int axis = 0; axis < 3; axis++) {
		float lo = clip_points[0][0][axis], hi = lo;
		for (int j = 1; j < num_clip_points; j++) {
			lo = Q_min(lo, clip_points[0][j][axis]);
			hi = Q_max(hi, clip_points[0][j][axis]);
		}
		if (hi < mins[axis] - 32 || lo > maxs[axis] + 32) {
			return;
		}
	}

	// chop the surface by all the bounding planes of the to be projected polygon
#endif // REND2_SP
	pingPong = 0;

#ifdef REND2_SP
	for (i = 0; i < numPlanes; i++) {
		R_ChopPolyBehindPlane(num_clip_points, clip_points[pingPong],
			&num_clip_points, clip_points[!pingPong],
			normals[i], dists[i], 0.5);
#else // !REND2_SP
	for (i = 0; i < numPlanes; i++) {
		R_ChopPolyBehindPlane(numClipPoints, clipPoints[pingPong],
			&numClipPoints, clipPoints[!pingPong],
			normals[i], dists[i], 0.5);
#endif // REND2_SP
		pingPong ^= 1;
#ifdef REND2_SP
		if (num_clip_points == 0) {
#else // !REND2_SP
		if (numClipPoints == 0) {
#endif // REND2_SP
			break;
		}
	}
	// completely clipped away?
#ifdef REND2_SP
	if (num_clip_points == 0) {
#else // !REND2_SP
	if (numClipPoints == 0) {
#endif // REND2_SP
		return;
	}

	// add this fragment to the returned list
#ifdef REND2_SP
	if (num_clip_points + (*returned_points) > max_points) {
#else // !REND2_SP
	if (numClipPoints + (*returnedPoints) > maxPoints) {
#endif // REND2_SP
		return;	// not enough space for this polygon
	}
	/*
	// all the clip points should be within the bounding box
	for ( i = 0 ; i < num_clip_points ; i++ ) {
		int j;
		for ( j = 0 ; j < 3 ; j++ ) {
			if (clip_points[pingPong][i][j] < mins[j] - 0.5) break;
			if (clip_points[pingPong][i][j] > maxs[j] + 0.5) break;
		}
		if (j < 3) break;
	}
	if (i < num_clip_points) return;
	*/

#ifdef REND2_SP
	mf = fragment_buffer + (*returned_fragments);
	mf->firstPoint = (*returned_points);
	mf->numPoints = num_clip_points;
	Com_Memcpy(point_buffer + (*returned_points) * 3, clip_points[pingPong], num_clip_points * sizeof(vec3_t));
#else // !REND2_SP
	mf = fragmentBuffer + (*returnedFragments);
	mf->firstPoint = (*returnedPoints);
	mf->numPoints = numClipPoints;
	Com_Memcpy(pointBuffer + (*returnedPoints) * 3, clipPoints[pingPong], numClipPoints * sizeof(vec3_t));
#endif // REND2_SP

#ifdef REND2_SP
	(*returned_points) += num_clip_points;
	(*returned_fragments)++;
#else // !REND2_SP
	(*returnedPoints) += numClipPoints;
	(*returnedFragments)++;
#endif // REND2_SP
}

/*
=================
R_MarkFragments

=================
*/
#ifdef REND2_SP
static bool R_MarkBoundsOutside(const vec3_t bmins, const vec3_t bmaxs, const vec3_t mins, const vec3_t maxs)
{
	return bmaxs[0] < mins[0] || bmins[0] > maxs[0]
		|| bmaxs[1] < mins[1] || bmins[1] > maxs[1]
		|| bmaxs[2] < mins[2] || bmins[2] > maxs[2];
}

static bool R_MarkTriangleOutside(const vec3_t a, const vec3_t b, const vec3_t c, const vec3_t mins, const vec3_t maxs)
{
	for (int axis = 0; axis < 3; axis++) {
		if (Q_max(a[axis], Q_max(b[axis], c[axis])) < mins[axis] || Q_min(a[axis], Q_min(b[axis], c[axis])) > maxs[axis]) {
			return true;
		}
	}
	return false;
}

int R_MarkFragments(int numPoints, const vec3_t* points, const vec3_t projection, const int max_points, vec3_t point_buffer, const int max_fragments, markFragment_t* fragment_buffer)
#else // !REND2_SP
int R_MarkFragments(int numPoints, const vec3_t* points, const vec3_t projection, const int maxPoints, vec3_t pointBuffer, const int maxFragments, markFragment_t* fragmentBuffer)
#endif // REND2_SP
{
	int				numsurfaces = 0, numPlanes = 0;
	int				i = 0, j = 0, k = 0, m = 0, n = 0;
	surfaceType_t* surfaces[64];
	vec3_t			mins, maxs;
#ifdef REND2_SP
	int				returned_fragments = 0;
	int				returned_points = 0;
	vec3_t			normals[MAX_VERTS_ON_POLY + 2];
	float			dists[MAX_VERTS_ON_POLY + 2];
	vec3_t			clip_points[2][MAX_VERTS_ON_POLY];
	int				num_clip_points = 0;
#else // !REND2_SP
	int				returnedFragments = 0;
	int				returnedPoints = 0;
	vec3_t			normals[MAX_VERTS_ON_POLY + 2]{};
	float			dists[MAX_VERTS_ON_POLY + 2]{};
	vec3_t			clipPoints[2][MAX_VERTS_ON_POLY]{};
	int				numClipPoints = 0;
#endif // REND2_SP
	float* v = nullptr;
	srfBspSurface_t* cv = nullptr;
	glIndex_t* tri = nullptr;
	srfVert_t* dv = nullptr;
	vec3_t			normal;
	vec3_t			projectionDir;
	vec3_t			v1, v2;

	if (numPoints <= 0) {
		return 0;
	}

	//increment view count for double check prevention
	tr.viewCount++;

	//
	VectorNormalize2(projection, projectionDir);
	// find all the brushes that are to be considered
	ClearBounds(mins, maxs);
	for (i = 0; i < numPoints; i++) {
		vec3_t	temp;

		AddPointToBounds(points[i], mins, maxs);
		VectorAdd(points[i], projection, temp);
		AddPointToBounds(temp, mins, maxs);
		// make sure we get all the leafs (also the one(s) in front of the hit surface)
		VectorMA(points[i], -20, projectionDir, temp);
		AddPointToBounds(temp, mins, maxs);
	}

	if (numPoints > MAX_VERTS_ON_POLY) numPoints = MAX_VERTS_ON_POLY;
	// create the bounding planes for the to be projected polygon
	for (i = 0; i < numPoints; i++) {
		VectorSubtract(points[(i + 1) % numPoints], points[i], v1);
		VectorAdd(points[i], projection, v2);
		VectorSubtract(points[i], v2, v2);
		CrossProduct(v1, v2, normals[i]);
		VectorNormalizeFast(normals[i]);
		dists[i] = DotProduct(normals[i], points[i]);
	}
	// add near and far clipping planes for projection
	VectorCopy(projectionDir, normals[numPoints]);
	dists[numPoints] = DotProduct(normals[numPoints], points[0]) - 32;
	VectorCopy(projectionDir, normals[numPoints + 1]);
	VectorInverse(normals[numPoints + 1]);
	dists[numPoints + 1] = DotProduct(normals[numPoints + 1], points[0]) - 20;
	numPlanes = numPoints + 2;

	numsurfaces = 0;
	R_BoxSurfaces_r(tr.world->nodes, mins, maxs, surfaces, 64, &numsurfaces, projectionDir);
	//assert(numsurfaces <= 64);
	//assert(numsurfaces != 64);

#ifdef REND2_SP
	// R_AddMarkFragments drops every polygon outside these bounds; test them here already so
	// whole grids and grid triangles far from the mark cost nothing (same result)
	vec3_t mark_mins, mark_maxs;
	for (i = 0; i < 3; i++) {
		mark_mins[i] = mins[i] - 32;
		mark_maxs[i] = maxs[i] + 32;
	}

	returned_points = 0;
	returned_fragments = 0;
#else // !REND2_SP
	returnedPoints = 0;
	returnedFragments = 0;
#endif // REND2_SP

	for (i = 0; i < numsurfaces; i++) {
		if (*surfaces[i] == SF_GRID) {
			cv = (srfBspSurface_t*)surfaces[i];
#ifdef REND2_SP
			if (R_MarkBoundsOutside(cv->cullBounds[0], cv->cullBounds[1], mark_mins, mark_maxs)) {
				continue;
			}
#endif // REND2_SP
			for (m = 0; m < cv->height - 1; m++) {
				for (n = 0; n < cv->width - 1; n++) {
					// We triangulate the grid and chop all triangles within
					// the bounding planes of the to be projected polygon.
					// LOD is not taken into account, not such a big deal though.
					//
					// It's probably much nicer to chop the grid itself and deal
					// with this grid as a normal SF_GRID surface so LOD will
					// be applied. However the LOD of that chopped grid must
					// be synced with the LOD of the original curve.
					// One way to do this; the chopped grid shares vertices with
					// the original curve. When LOD is applied to the original
					// curve the unused vertices are flagged. Now the chopped curve
					// should skip the flagged vertices. This still leaves the
					// problems with the vertices at the chopped grid edges.
					//
					// To avoid issues when LOD applied to "hollow curves" (like
					// the ones around many jump pads) we now just add a 2 unit
					// offset to the triangle vertices.
					// The offset is added in the vertex normal vector direction
					// so all triangles will still fit together.
					// The 2 unit offset should avoid pretty much all LOD problems.

#ifdef REND2_SP
					num_clip_points = 3;
#else // !REND2_SP
					numClipPoints = 3;
#endif // REND2_SP

					dv = cv->verts + m * cv->width + n;

#ifndef REND2_SP
					VectorCopy(dv[0].xyz, clipPoints[0][0]);
					VectorMA(clipPoints[0][0], MARKER_OFFSET, dv[0].normal, clipPoints[0][0]);
					VectorCopy(dv[cv->width].xyz, clipPoints[0][1]);
					VectorMA(clipPoints[0][1], MARKER_OFFSET, dv[cv->width].normal, clipPoints[0][1]);
					VectorCopy(dv[1].xyz, clipPoints[0][2]);
					VectorMA(clipPoints[0][2], MARKER_OFFSET, dv[1].normal, clipPoints[0][2]);
#endif // !REND2_SP
					// both triangles of this cell far from the mark -> nothing to add (MARKER_OFFSET is 0)
#ifdef REND2_SP
					if (R_MarkTriangleOutside(dv[0].xyz, dv[cv->width].xyz, dv[1].xyz, mark_mins, mark_maxs)
						&& R_MarkTriangleOutside(dv[1].xyz, dv[cv->width].xyz, dv[cv->width + 1].xyz, mark_mins, mark_maxs)) {
						continue;
					}

					VectorCopy(dv[0].xyz, clip_points[0][0]);
					VectorMA(clip_points[0][0], MARKER_OFFSET, dv[0].normal, clip_points[0][0]);
					VectorCopy(dv[cv->width].xyz, clip_points[0][1]);
					VectorMA(clip_points[0][1], MARKER_OFFSET, dv[cv->width].normal, clip_points[0][1]);
					VectorCopy(dv[1].xyz, clip_points[0][2]);
					VectorMA(clip_points[0][2], MARKER_OFFSET, dv[1].normal, clip_points[0][2]);
					// check the normal of this triangle
					VectorSubtract(clip_points[0][0], clip_points[0][1], v1);
					VectorSubtract(clip_points[0][2], clip_points[0][1], v2);
#else // !REND2_SP
					VectorSubtract(clipPoints[0][0], clipPoints[0][1], v1);
					VectorSubtract(clipPoints[0][2], clipPoints[0][1], v2);
#endif // REND2_SP
					CrossProduct(v1, v2, normal);
					VectorNormalizeFast(normal);
					if (DotProduct(normal, projectionDir) < -0.1) {
#ifdef REND2_SP
						// add the fragments of this triangle
						R_AddMarkFragments(num_clip_points, clip_points,
							numPlanes, normals, dists,
							max_points, point_buffer,
							max_fragments, fragment_buffer,
							&returned_points, &returned_fragments, mins, maxs);
#else // !REND2_SP
						// add the fragments of this triangle
						R_AddMarkFragments(numClipPoints, clipPoints,
							numPlanes, normals, dists,
							maxPoints, pointBuffer,
							maxFragments, fragmentBuffer,
							&returnedPoints, &returnedFragments, mins, maxs);
#endif // REND2_SP

#ifdef REND2_SP
						if (returned_fragments == max_fragments) {
							return returned_fragments;	// not enough space for more fragments
#else // !REND2_SP
						if (returnedFragments == maxFragments) {
							return returnedFragments;	// not enough space for more fragments
#endif // REND2_SP
						}
					}

#ifdef REND2_SP
					VectorCopy(dv[1].xyz, clip_points[0][0]);
					VectorMA(clip_points[0][0], MARKER_OFFSET, dv[1].normal, clip_points[0][0]);
					VectorCopy(dv[cv->width].xyz, clip_points[0][1]);
					VectorMA(clip_points[0][1], MARKER_OFFSET, dv[cv->width].normal, clip_points[0][1]);
					VectorCopy(dv[cv->width + 1].xyz, clip_points[0][2]);
					VectorMA(clip_points[0][2], MARKER_OFFSET, dv[cv->width + 1].normal, clip_points[0][2]);
#else // !REND2_SP
					VectorCopy(dv[1].xyz, clipPoints[0][0]);
					VectorMA(clipPoints[0][0], MARKER_OFFSET, dv[1].normal, clipPoints[0][0]);
					VectorCopy(dv[cv->width].xyz, clipPoints[0][1]);
					VectorMA(clipPoints[0][1], MARKER_OFFSET, dv[cv->width].normal, clipPoints[0][1]);
					VectorCopy(dv[cv->width + 1].xyz, clipPoints[0][2]);
					VectorMA(clipPoints[0][2], MARKER_OFFSET, dv[cv->width + 1].normal, clipPoints[0][2]);
#endif // REND2_SP
					// check the normal of this triangle
#ifdef REND2_SP
					VectorSubtract(clip_points[0][0], clip_points[0][1], v1);
					VectorSubtract(clip_points[0][2], clip_points[0][1], v2);
#else // !REND2_SP
					VectorSubtract(clipPoints[0][0], clipPoints[0][1], v1);
					VectorSubtract(clipPoints[0][2], clipPoints[0][1], v2);
#endif // REND2_SP
					CrossProduct(v1, v2, normal);
					VectorNormalizeFast(normal);
					if (DotProduct(normal, projectionDir) < -0.05) {
#ifdef REND2_SP
						// add the fragments of this triangle
						R_AddMarkFragments(num_clip_points, clip_points,
							numPlanes, normals, dists,
							max_points, point_buffer,
							max_fragments, fragment_buffer,
							&returned_points, &returned_fragments, mins, maxs);
#else // !REND2_SP
						// add the fragments of this triangle
						R_AddMarkFragments(numClipPoints, clipPoints,
							numPlanes, normals, dists,
							maxPoints, pointBuffer,
							maxFragments, fragmentBuffer,
							&returnedPoints, &returnedFragments, mins, maxs);
#endif // REND2_SP

#ifdef REND2_SP
						if (returned_fragments == max_fragments) {
							return returned_fragments;	// not enough space for more fragments
#else // !REND2_SP
						if (returnedFragments == maxFragments) {
							return returnedFragments;	// not enough space for more fragments
#endif // REND2_SP
						}
					}
				}
			}
		}
		else if (*surfaces[i] == SF_FACE) {
			srfBspSurface_t* surf = (srfBspSurface_t*)surfaces[i];

			// check the normal of this face
			if (DotProduct(surf->cullPlane.normal, projectionDir) > -0.5) {
				continue;
			}

			for (k = 0, tri = surf->indexes; k < surf->numIndexes; k += 3, tri += 3)
			{
				for (j = 0; j < 3; j++)
				{
					v = surf->verts[tri[j]].xyz;
#ifdef REND2_SP
					VectorMA(v, MARKER_OFFSET, surf->cullPlane.normal, clip_points[0][j]);
#else // !REND2_SP
					VectorMA(v, MARKER_OFFSET, surf->cullPlane.normal, clipPoints[0][j]);
#endif // REND2_SP
				}

#ifdef REND2_SP
				// add the fragments of this face
				R_AddMarkFragments(3, clip_points,
					numPlanes, normals, dists,
					max_points, point_buffer,
					max_fragments, fragment_buffer,
					&returned_points, &returned_fragments, mins, maxs);
				if (returned_fragments == max_fragments) {
					return returned_fragments;	// not enough space for more fragments
#else // !REND2_SP
				// add the fragments of this face
				R_AddMarkFragments(3, clipPoints,
					numPlanes, normals, dists,
					maxPoints, pointBuffer,
					maxFragments, fragmentBuffer,
					&returnedPoints, &returnedFragments, mins, maxs);
				if (returnedFragments == maxFragments) {
					return returnedFragments;	// not enough space for more fragments
#endif // REND2_SP
				}
			}
		}
		else if (*surfaces[i] == SF_TRIANGLES && r_marksOnTriangleMeshes->integer) {
			srfBspSurface_t* surf = (srfBspSurface_t*)surfaces[i];

			for (k = 0, tri = surf->indexes; k < surf->numIndexes; k += 3, tri += 3)
			{
				for (j = 0; j < 3; j++)
				{
					v = surf->verts[tri[j]].xyz;
#ifdef REND2_SP
					VectorMA(v, MARKER_OFFSET, surf->verts[tri[j]].normal, clip_points[0][j]);
#else // !REND2_SP
					VectorMA(v, MARKER_OFFSET, surf->verts[tri[j]].normal, clipPoints[0][j]);
#endif // REND2_SP
				}

#ifdef REND2_SP
				// add the fragments of this face
				R_AddMarkFragments(3, clip_points,
					numPlanes, normals, dists,
					max_points, point_buffer,
					max_fragments, fragment_buffer, &returned_points, &returned_fragments, mins, maxs);
				if (returned_fragments == max_fragments)
#else // !REND2_SP
				// add the fragments of this face
				R_AddMarkFragments(3, clipPoints,
					numPlanes, normals, dists,
					maxPoints, pointBuffer,
					maxFragments, fragmentBuffer, &returnedPoints, &returnedFragments, mins, maxs);
				if (returnedFragments == maxFragments)
#endif // REND2_SP
				{
#ifdef REND2_SP
					return returned_fragments;	// not enough space for more fragments
#else // !REND2_SP
					return returnedFragments;	// not enough space for more fragments
#endif // REND2_SP
				}
			}
		}
	}
#ifdef REND2_SP
	return returned_fragments;
#else // !REND2_SP
	return returnedFragments;
#endif // REND2_SP
}
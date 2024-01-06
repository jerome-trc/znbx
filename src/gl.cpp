/// @file
/// @brief Routines only necessary for building GL-friendly nodes.

/*

Copyright (C) 2002-2006 Randy Heit

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#include <assert.h>

#include "common.hpp"
#include "nodebuild.hpp"

#define Printf printf

#ifdef ZNBX_DEBUG_VERBOSE
#include <stdio.h>
#define D(x) x
#else
#define D(x) \
	do {     \
	} while (0)
#endif

double FNodeBuilder::AddIntersection(const znbx_NodeFxp& node, int vertex) {
	static const FEventInfo defaultInfo = { -1, UINT_MAX };

	// Calculate signed distance of intersection vertex from start of splitter.
	// Only ordering is important, so we don't need a sqrt.
	FPrivVert* v = &Vertices[vertex];
	double dist = (double(v->x) - node.x) * (node.dx) + (double(v->y) - node.y) * (node.dy);

	FEvent* event = Events.FindEvent(dist);

	if (event == NULL) {
		event = Events.GetNewNode();
		event->Distance = dist;
		event->Info = defaultInfo;
		event->Info.Vertex = vertex;
		Events.Insert(event);
	}

	return dist;
}

// If there are any segs on the splitter that span more than two events, they
// must be split. Alien Vendetta is one example wad that is quite bad about
// having overlapping lines. If we skip this step, these segs will still be
// split later, but minisegs will erroneously be added for them, and partner
// seg information will be messed up in the generated tree.
// If there are any segs on the splitter that span more than two events, they
// must be split. Alien Vendetta is one example wad that is quite bad about
// having overlapping lines. If we skip this step, these segs will still be
// split later, but minisegs will erroneously be added for them, and partner
// seg information will be messed up in the generated tree.
void FNodeBuilder::FixSplitSharers([[maybe_unused]] const znbx_NodeFxp& node) {
	for (unsigned int i = 0; i < SplitSharers.Size(); ++i) {
		uint32_t seg = SplitSharers[i].Seg;
		int v2 = Segs[seg].v2;
		FEvent* event = Events.FindEvent(SplitSharers[i].Distance);
		FEvent* next;

		if (event == NULL) { // Should not happen
			continue;
		}

		if (SplitSharers[i].Forward) {
			event = Events.GetSuccessor(event);
			if (event == NULL) {
				continue;
			}
			next = Events.GetSuccessor(event);
		} else {
			event = Events.GetPredecessor(event);
			if (event == NULL) {
				continue;
			}
			next = Events.GetPredecessor(event);
		}

		while (event != NULL && next != NULL && event->Info.Vertex != v2) {
			uint32_t newseg = SplitSeg(seg, event->Info.Vertex, 1);

			Segs[newseg].next = Segs[seg].next;
			Segs[seg].next = newseg;

			uint32_t partner = Segs[seg].partner;
			if (partner != UINT_MAX) {
				int endpartner = SplitSeg(partner, event->Info.Vertex, 1);

				Segs[endpartner].next = Segs[partner].next;
				Segs[partner].next = endpartner;

				Segs[seg].partner = endpartner;
				Segs[partner].partner = newseg;
			}

			seg = newseg;
			if (SplitSharers[i].Forward) {
				event = next;
				next = Events.GetSuccessor(next);
			} else {
				event = next;
				next = Events.GetPredecessor(next);
			}
		}
	}
}

void FNodeBuilder::AddMinisegs(
	const znbx_NodeFxp& node, uint32_t splitseg, uint32_t& fset, uint32_t& bset
) {
	FEvent *event = Events.GetMinimum(), *prev = NULL;

	while (event != NULL) {
		if (prev != NULL) {
			uint32_t fseg1, bseg1, fseg2, bseg2;
			uint32_t fnseg, bnseg;

			// Minisegs should only be added when they can create valid loops on both the front and
			// back of the splitter. This means some subsectors could be unclosed if their sectors
			// are unclosed, but at least we won't be needlessly creating subsectors in void space.
			// Unclosed subsectors can be closed trivially once the BSP tree is complete.

			if ((fseg1 = CheckLoopStart(node.dx, node.dy, prev->Info.Vertex, event->Info.Vertex)) !=
					UINT_MAX &&
				(bseg1 = CheckLoopStart(-node.dx, -node.dy, event->Info.Vertex, prev->Info.Vertex)
				) != UINT_MAX &&
				(fseg2 = CheckLoopEnd(node.dx, node.dy, event->Info.Vertex)) != UINT_MAX &&
				(bseg2 = CheckLoopEnd(-node.dx, -node.dy, prev->Info.Vertex)) != UINT_MAX) {
				// Add miniseg on the front side
				fnseg =
					AddMiniseg(prev->Info.Vertex, event->Info.Vertex, UINT_MAX, fseg1, splitseg);
				Segs[fnseg].next = fset;
				fset = fnseg;

				// Add miniseg on the back side
				bnseg = AddMiniseg(event->Info.Vertex, prev->Info.Vertex, fnseg, bseg1, splitseg);
				Segs[bnseg].next = bset;
				bset = bnseg;

				int fsector, bsector;

				fsector = Segs[fseg1].frontsector;
				bsector = Segs[bseg1].frontsector;

				Segs[fnseg].frontsector = fsector;
				Segs[fnseg].backsector = bsector;
				Segs[bnseg].frontsector = bsector;
				Segs[bnseg].backsector = fsector;

				// Only print the warning if this might be bad.
				if (fsector != bsector && fsector != Segs[fseg1].backsector &&
					bsector != Segs[bseg1].backsector) {
#if 0 // TODO: emit warnings some other way.
					Warn ("Sectors %d at (%d,%d) and %d at (%d,%d) don't match.\n",
						Segs[fseg1].frontsector,
						Vertices[prev->Info.Vertex].x>>FRACBITS, Vertices[prev->Info.Vertex].y>>FRACBITS,
						Segs[bseg1].frontsector,
						Vertices[event->Info.Vertex].x>>FRACBITS, Vertices[event->Info.Vertex].y>>FRACBITS
						);
#endif
				}

				D(Printf(
					"**Minisegs** %d/%d added %d(%d,%d)->%d(%d,%d)\n", fnseg, bnseg,
					prev->Info.Vertex, Vertices[prev->Info.Vertex].x >> 16,
					Vertices[prev->Info.Vertex].y >> 16, event->Info.Vertex,
					Vertices[event->Info.Vertex].x >> 16, Vertices[event->Info.Vertex].y >> 16
				));
			}
		}
		prev = event;
		event = Events.GetSuccessor(event);
	}
}

uint32_t FNodeBuilder::AddMiniseg(int v1, int v2, uint32_t partner, uint32_t seg1, uint32_t splitseg) {
	uint32_t nseg;
	FPrivSeg* seg = &Segs[seg1];
	FPrivSeg newseg;

	newseg.sidedef = NO_INDEX;
	newseg.linedef = NO_INDEX;
	newseg.loopnum = 0;
	newseg.next = UINT_MAX;
	newseg.planefront = true;
	newseg.hashnext = NULL;
	newseg.storedseg = UINT_MAX;
	newseg.frontsector = -1;
	newseg.backsector = -1;
	newseg.offset = 0;
	newseg.angle = 0;

	if (splitseg != UINT_MAX) {
		newseg.planenum = Segs[splitseg].planenum;
	} else {
		newseg.planenum = -1;
	}

	newseg.v1 = v1;
	newseg.v2 = v2;
	newseg.nextforvert = Vertices[v1].segs;
	newseg.nextforvert2 = Vertices[v2].segs2;
	newseg.next = seg->next;
	if (partner != UINT_MAX) {
		newseg.partner = partner;

		assert(Segs[partner].v1 == newseg.v2);
		assert(Segs[partner].v2 == newseg.v1);
	} else {
		newseg.partner = UINT_MAX;
	}
	nseg = Segs.Push(newseg);
	if (newseg.partner != UINT_MAX) {
		Segs[partner].partner = nseg;
	}
	Vertices[v1].segs = nseg;
	Vertices[v2].segs2 = nseg;
	// Printf ("Between %d and %d::::\n", seg1, seg2);
	return nseg;
}

uint32_t FNodeBuilder::CheckLoopStart(znbx_I16F16 dx, znbx_I16F16 dy, int vertex, int vertex2) {
	FPrivVert* v = &Vertices[vertex];
	znbx_Angle splitAngle = PointToAngle(dx, dy);
	uint32_t segnum;
	znbx_Angle bestang;
	uint32_t bestseg;

	// Find the seg ending at this vertex that forms the smallest angle
	// to the splitter.
	segnum = v->segs2;
	bestang = ANGLE_MAX;
	bestseg = UINT_MAX;
	while (segnum != UINT_MAX) {
		FPrivSeg* seg = &Segs[segnum];
		znbx_Angle segAngle = PointToAngle(Vertices[seg->v1].x - v->x, Vertices[seg->v1].y - v->y);
		znbx_Angle diff = splitAngle - segAngle;

		if (diff < ANGLE_EPSILON &&
			PointOnSide(Vertices[seg->v1].x, Vertices[seg->v1].y, v->x, v->y, dx, dy) == 0) {
			// If a seg lies right on the splitter, don't count it
		} else {
			if (diff <= bestang) {
				bestang = diff;
				bestseg = segnum;
			}
		}
		segnum = seg->nextforvert2;
	}
	if (bestseg == UINT_MAX) {
		return UINT_MAX;
	}
	// Now make sure there are no segs starting at this vertex that form
	// an even smaller angle to the splitter.
	segnum = v->segs;
	while (segnum != UINT_MAX) {
		FPrivSeg* seg = &Segs[segnum];
		if (seg->v2 == vertex2) {
			return UINT_MAX;
		}
		znbx_Angle segAngle = PointToAngle(Vertices[seg->v2].x - v->x, Vertices[seg->v2].y - v->y);
		znbx_Angle diff = splitAngle - segAngle;
		if (diff < bestang && seg->partner != bestseg) {
			return UINT_MAX;
		}
		segnum = seg->nextforvert;
	}
	return bestseg;
}

uint32_t FNodeBuilder::CheckLoopEnd(znbx_I16F16 dx, znbx_I16F16 dy, int vertex) {
	FPrivVert* v = &Vertices[vertex];
	znbx_Angle splitAngle = PointToAngle(dx, dy) + ANGLE_180;
	uint32_t segnum;
	znbx_Angle bestang;
	uint32_t bestseg;

	// Find the seg starting at this vertex that forms the smallest angle
	// to the splitter.
	segnum = v->segs;
	bestang = ANGLE_MAX;
	bestseg = UINT_MAX;
	while (segnum != UINT_MAX) {
		FPrivSeg* seg = &Segs[segnum];
		znbx_Angle segAngle = PointToAngle(Vertices[seg->v2].x - v->x, Vertices[seg->v2].y - v->y);
		znbx_Angle diff = segAngle - splitAngle;

		if (diff < ANGLE_EPSILON &&
			PointOnSide(Vertices[seg->v1].x, Vertices[seg->v1].y, v->x, v->y, dx, dy) == 0) {
			// If a seg lies right on the splitter, don't count it
		} else {
			if (diff <= bestang) {
				bestang = diff;
				bestseg = segnum;
			}
		}
		segnum = seg->nextforvert;
	}
	if (bestseg == UINT_MAX) {
		return UINT_MAX;
	}
	// Now make sure there are no segs ending at this vertex that form
	// an even smaller angle to the splitter.
	segnum = v->segs2;
	while (segnum != UINT_MAX) {
		FPrivSeg* seg = &Segs[segnum];
		znbx_Angle segAngle = PointToAngle(Vertices[seg->v1].x - v->x, Vertices[seg->v1].y - v->y);
		znbx_Angle diff = segAngle - splitAngle;
		if (diff < bestang && seg->partner != bestseg) {
			return UINT_MAX;
		}
		segnum = seg->nextforvert2;
	}
	return bestseg;
}

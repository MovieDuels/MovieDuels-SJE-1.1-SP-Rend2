/*
===========================================================================
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2013 - 2015, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

#include "g_local.h"
#include "g_functions.h"
#include "b_local.h"

extern void G_MoverTouchPushTriggers(gentity_t* ent, vec3_t old_org);
void G_StopObjectMoving(gentity_t* object);

/*
================
G_BounceMissile

================
*/
static void G_BounceObject(gentity_t* ent, const trace_t* trace)
{
	vec3_t velocity;

	// reflect the velocity on the trace plane
	const int hit_time = level.previousTime + (level.time - level.previousTime) * trace->fraction;
	EvaluateTrajectoryDelta(&ent->s.pos, hit_time, velocity);
	const float dot = DotProduct(velocity, trace->plane.normal);
	float bounceFactor = 60 / ent->mass;
	if (bounceFactor > 1.0f)
	{
		bounceFactor = 1.0f;
	}
	VectorMA(velocity, -2 * dot * bounceFactor, trace->plane.normal, ent->s.pos.trDelta);

	//FIXME: customized or material-based impact/bounce sounds
	if (ent->s.eFlags & EF_BOUNCE_HALF)
	{
		VectorScale(ent->s.pos.trDelta, 0.5, ent->s.pos.trDelta);

		// check for stop
		if ((trace->plane.normal[2] > 0.7 && g_gravity->value > 0 || trace->plane.normal[2] < -0.7 && g_gravity->
			value < 0) && (ent->s.pos.trDelta[2] < 40 && g_gravity->value > 0 || ent->s.pos.trDelta[2] > -40 &&
				g_gravity->value < 0))
			//this can happen even on very slightly sloped walls, so changed it from > 0 to > 0.7
		{
			ent->s.apos.trType = TR_STATIONARY;
			VectorCopy(ent->currentAngles, ent->s.apos.trBase);
			VectorCopy(trace->endpos, ent->currentOrigin);
			VectorCopy(trace->endpos, ent->s.pos.trBase);
			ent->s.pos.trTime = level.time;
			return;
		}
	}

	// NEW--It would seem that we want to set our trBase to the trace end pos
	//	and set the trTime to the actual time of impact....
	//	FIXME: Should we still consider adding the normal though??
	VectorCopy(trace->endpos, ent->currentOrigin);
	ent->s.pos.trTime = hit_time;

	VectorCopy(ent->currentOrigin, ent->s.pos.trBase);
	VectorCopy(trace->plane.normal, ent->pos1); //???
}

/*
================
G_RunObject
================
*/
extern void DoImpact(gentity_t* self, gentity_t* other, qboolean damage_self, const trace_t* trace);

void G_RunObject(gentity_t* ent)
{
	vec3_t origin, old_org;
	trace_t tr;

	//FIXME: floaters need to stop floating up after a while, even if gravity stays negative?
	if (ent->s.pos.trType == TR_STATIONARY) //g_gravity->value <= 0 &&
	{
		ent->s.pos.trType = TR_GRAVITY;
		VectorCopy(ent->currentOrigin, ent->s.pos.trBase);
		ent->s.pos.trTime = level.previousTime; //?necc?
		if (!g_gravity->value)
		{
			ent->s.pos.trDelta[2] += 100;
		}
	}

	ent->nextthink = level.time + FRAMETIME;

	VectorCopy(ent->currentOrigin, old_org);
	// get current position
	EvaluateTrajectory(&ent->s.pos, level.time, origin);
	//Get current angles?
	EvaluateTrajectory(&ent->s.apos, level.time, ent->currentAngles);

	if (VectorCompare(ent->currentOrigin, origin))
	{
		//error - didn't move at all!
		return;
	}
	// trace a line from the previous position to the current position,
	// ignoring interactions with the missile owner
	gi.trace(&tr, ent->currentOrigin, ent->mins, ent->maxs, origin,
		ent->owner ? ent->owner->s.number : ent->s.number, ent->clipmask, static_cast<EG2_Collision>(0), 0);

	if (!tr.startsolid && !tr.allsolid && tr.fraction)
	{
		VectorCopy(tr.endpos, ent->currentOrigin);
		gi.linkentity(ent);
	}
	else
	{
		tr.fraction = 0;
	}

	G_MoverTouchPushTriggers(ent, old_org);

	if (tr.fraction == 1)
	{
		if (g_gravity->value <= 0)
		{
			if (ent->s.apos.trType == TR_STATIONARY)
			{
				VectorCopy(ent->currentAngles, ent->s.apos.trBase);
				ent->s.apos.trType = TR_LINEAR;
				ent->s.apos.trDelta[1] = Q_flrand(-300, 300);
				ent->s.apos.trDelta[0] = Q_flrand(-10, 10);
				ent->s.apos.trDelta[2] = Q_flrand(-10, 10);
				ent->s.apos.trTime = level.time;
			}
		}
		//friction in zero-G
		if (!g_gravity->value)
		{
			constexpr float friction = 0.975f;
			/*friction -= ent->mass/1000.0f;
			if ( friction < 0.1 )
			{
				friction = 0.1f;
			}
			*/
			VectorScale(ent->s.pos.trDelta, friction, ent->s.pos.trDelta);
			VectorCopy(ent->currentOrigin, ent->s.pos.trBase);
			ent->s.pos.trTime = level.time;
		}
		return;
	}

	//hit something

	//Do impact damage
	gentity_t* traceEnt = &g_entities[tr.entityNum];
	if (tr.fraction || traceEnt && traceEnt->takedamage)
	{
		if (!VectorCompare(ent->currentOrigin, old_org))
		{
			//moved and impacted
			if (traceEnt && traceEnt->takedamage)
			{
				//hurt someone
				vec3_t fx_dir;
				VectorNormalize2(ent->s.pos.trDelta, fx_dir);
				VectorScale(fx_dir, -1, fx_dir);
				G_PlayEffect(G_EffectIndex("melee/kick_impact"), tr.endpos, fx_dir);
				//G_Sound( ent, G_SoundIndex( va( "sound/weapons/melee/punch%d", Q_irand( 1, 4 ) ) ) );
			}
			else
			{
				G_PlayEffect(G_EffectIndex("melee/kick_impact_silent"), tr.endpos, tr.plane.normal);
			}
			if (ent->mass > 100)
			{
				G_Sound(ent, G_SoundIndex("sound/movers/objects/objectHitHeavy.wav"));
			}
			else
			{
				G_Sound(ent, G_SoundIndex("sound/movers/objects/objectHit.wav"));
			}
		}
		DoImpact(ent, traceEnt, static_cast<qboolean>(!(tr.surfaceFlags & SURF_NODAMAGE)), &tr);
	}

	if (!ent || ent->takedamage && ent->health <= 0)
	{
		//been destroyed by impact
		//chunks?
		G_Sound(ent, G_SoundIndex("sound/movers/objects/objectBreak.wav"));
		return;
	}

	//do impact physics
	if (ent->s.pos.trType == TR_GRAVITY) //tr.fraction < 1.0 &&
	{
		//FIXME: only do this if no trDelta
		if (g_gravity->value <= 0 || tr.plane.normal[2] < 0.7)
		{
			if (ent->s.eFlags & (EF_BOUNCE | EF_BOUNCE_HALF))
			{
				if (tr.fraction <= 0.0f)
				{
					VectorCopy(tr.endpos, ent->currentOrigin);
					VectorCopy(tr.endpos, ent->s.pos.trBase);
					VectorClear(ent->s.pos.trDelta);
					ent->s.pos.trTime = level.time;
				}
				else
				{
					G_BounceObject(ent, &tr);
				}
			}
			else
			{
				//slide down?
				//FIXME: slide off the slope
			}
		}
		else
		{
			ent->s.apos.trType = TR_STATIONARY;
			pitch_roll_for_slope(ent, tr.plane.normal);
			//ent->currentAngles[0] = 0;//FIXME: match to slope
			//ent->currentAngles[2] = 0;//FIXME: match to slope
			VectorCopy(ent->currentAngles, ent->s.apos.trBase);
			//okay, we hit the floor, might as well stop or prediction will
			//make us go through the floor!
			//FIXME: this means we can't fall if something is pulled out from under us...
			G_StopObjectMoving(ent);
		}
	}
	else
	{
		ent->s.apos.trType = TR_STATIONARY;
		pitch_roll_for_slope(ent, tr.plane.normal);
		//ent->currentAngles[0] = 0;//FIXME: match to slope
		//ent->currentAngles[2] = 0;//FIXME: match to slope
		VectorCopy(ent->currentAngles, ent->s.apos.trBase);
	}

	//call touch func
	GEntity_TouchFunc(ent, &g_entities[tr.entityNum], &tr);
}

void G_StopObjectMoving(gentity_t* object)
{
	object->s.pos.trType = TR_STATIONARY;
	VectorCopy(object->currentOrigin, object->s.origin);
	VectorCopy(object->currentOrigin, object->s.pos.trBase);
	VectorClear(object->s.pos.trDelta);
}

static void G_StartObjectMoving(gentity_t* object, vec3_t dir, const float speed, const trType_t tr_type)
{
	VectorNormalize(dir);

	//object->s.eType = ET_GENERAL;
	object->s.pos.trType = tr_type;
	VectorCopy(object->currentOrigin, object->s.pos.trBase);
	VectorScale(dir, speed, object->s.pos.trDelta);
	object->s.pos.trTime = level.time;

	//FIXME: make these objects go through G_RunObject automatically, like missiles do
	if (object->e_ThinkFunc == thinkF_NULL)
	{
		object->nextthink = level.time + FRAMETIME;
		object->e_ThinkFunc = thinkF_G_RunObject;
	}
	else
	{
		//You're responsible for calling RunObject
	}
}

gentity_t* G_CreateObject(gentity_t* owner, vec3_t origin, vec3_t angles, const int modelIndex, const int frame,
	const trType_t tr_type,
	const int effect_id = 0)
{
	gentity_t* object = G_Spawn();

	if (object == nullptr)
	{
		return nullptr;
	}

	object->classname = "object"; //?
	object->nextthink = level.time + FRAMETIME;
	object->e_ThinkFunc = thinkF_G_RunObject;
	object->s.eType = ET_GENERAL;
	object->s.eFlags |= EF_AUTO_SIZE; //CG_Ents will create the mins & max itself based on model bounds
	object->s.modelindex = modelIndex;
	//FIXME: allow to set a targetname/script_targetname and animation info?
	object->s.frame = object->startFrame = object->endFrame = frame;
	object->owner = owner;
	object->clipmask = MASK_SOLID;

	// The effect to play.
	object->count = effect_id;

	//Give it SOME size for now
	VectorSet(object->mins, -4, -4, -4);
	VectorSet(object->maxs, 4, 4, 4);

	//Origin
	G_SetOrigin(object, origin);
	object->s.pos.trType = tr_type;
	VectorCopy(origin, object->s.pos.trBase);
	//Velocity
	VectorClear(object->s.pos.trDelta);
	object->s.pos.trTime = level.time;

	//Angles
	VectorCopy(angles, object->s.angles);
	VectorCopy(object->s.angles, object->s.apos.trBase);
	//Angular Velocity
	VectorClear(object->s.apos.trDelta);
	object->s.apos.trTime = level.time;

	gi.linkentity(object);

	return object;
}
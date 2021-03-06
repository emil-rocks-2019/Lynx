#include <stdio.h>
#include "WorldClient.h"
#include "ServerClient.h"
#include "lynxsys.h"

#ifdef _DEBUG
#include <crtdbg.h>
#define new new(_NORMAL_BLOCK,__FILE__, __LINE__)
#endif

#pragma warning(disable: 4355)
CWorldClient::CWorldClient(void) : m_ghostobj(this)
{
    m_localobj = &m_ghostobj;
    m_ghostobj.SetOrigin(vec3_t(0,1000.0f,0));

    m_interpworld.m_pbsp = &m_bsptree; // FIXME is this save at a level change?
    m_interpworld.m_presman = &m_resman;
    m_interpworld.state1.localtime = 0;
    m_interpworld.state2.localtime = 0;
}

CWorldClient::~CWorldClient(void)
{
}

CObj* CWorldClient::GetLocalObj() const
{
    return m_localobj;
}

void CWorldClient::SetLocalObj(int id)
{
    CObj* obj;

    if(id == 0)
    {
        m_localobj = &m_ghostobj;
    }
    else
    {
        obj = GetObj(id);
        assert(obj);
        if(m_localobj->GetID() != id)
        {
            m_ghostobj.CopyObjStateFrom(obj);
        }else if((m_ghostobj.GetOrigin()-m_localobj->GetOrigin()).AbsSquared() >= MAX_SV_CL_POS_DIFF)
        {
            fprintf(stderr, "CL: Reset position\n");
            m_ghostobj.SetOrigin(m_localobj->GetOrigin());
        }
        if(obj)
            m_localobj = obj;
    }
}

void CWorldClient::Update(const float dt, const uint32_t ticks)
{
    CWorld::Update(dt, ticks);

    CObj* controller = GetLocalController();
    ObjMove(controller, dt);

    if(m_interpworld.f >= 1.0f)
        CreateClientInterp();
    m_interpworld.Update(dt, ticks);
}

bool CWorldClient::Serialize(bool write, CStream* stream, const world_state_t* oldstate)
{
    bool changed = CWorld::Serialize(write, stream, oldstate);
    if(changed && !write)
        AddWorldToHistory();

    return changed;
}

void CWorldClient::AddWorldToHistory()
{
    worldclient_state_t clstate;
    clstate.state = GetWorldState();
    clstate.localtime = CLynxSys::GetTicks();
    m_history.push_front(clstate);
    if(m_history.size() > 80)
        fprintf(stderr,
                "Client snapshot history buffer size is getting larger: %u\n",
                (uint32_t)m_history.size());
    while(clstate.localtime - m_history.back().localtime > MAX_CLIENT_HISTORY)
        m_history.pop_back();
    while(m_history.size() > 80)
        m_history.pop_back();
    assert(m_history.size() <= 80);
}

/*
    Vorbereiten von Interpolierter Welt
 */
void CWorldClient::CreateClientInterp()
{
    if(m_history.size() < 2)
        return;

    std::list<worldclient_state_t>::iterator iter = m_history.begin();
    const uint32_t tlocal = CLynxSys::GetTicks(); // current time
    const uint32_t tlocal_n = (*iter).localtime;  // time of last packet
    const uint32_t dtupdate = tlocal - tlocal_n;  // time since last update
    const uint32_t rendertime = tlocal - RENDER_DELAY; // interpolation point

    if(dtupdate > RENDER_DELAY)
    {
        //if(m_history.size() > 2)
        //  fprintf(stderr, "CWorldClient: Server lag, no update since %i ms.\n", dtupdate);
        return;
    }

    // Jetzt werden die beiden worldclient_state_t Objekte gesucht, die um den Renderzeitpunkt liegen
    // Wenn es das nicht gibt, muss extrapoliert werden

    // LINEARE INTERPOLATION

    std::list<worldclient_state_t>::iterator state1 = iter; // worldstate vor rendertime
    std::list<worldclient_state_t>::iterator state2 = iter; // worldstate nach rendertime
    for(iter = m_history.begin();iter != m_history.end();iter++)
    {
        if((*iter).localtime < rendertime)
        {
            state1 = iter;
            break;
        }
        else
            state2 = iter;
    }
    if(state1 == state2)
        return;
    if((*state1).localtime > rendertime)
    {
        return;
    }
    if(state1 == m_history.end() || state2 == m_history.end())
    {
        assert(0);
        return;
    }

    worldclient_state_t w1 = (*state1);
    worldclient_state_t w2 = (*state2);
    assert(w1.localtime < rendertime && w2.localtime >= rendertime);

    m_interpworld.state1 = w1;
    m_interpworld.state2 = w2;

    CObj* obj;
    WORLD_STATE_OBJITER objiter;
    for(objiter =  w1.state.ObjBegin();
        objiter != w1.state.ObjEnd(); objiter++)
    {
        int id = (*objiter).first;

        // Pr�fen, ob Obj auch in w2 vorkommt
        if(!w2.state.ObjStateExists(id))
        {
            //assert(0); // OK soweit?
            // K�nnte man hier gleich l�schen und die Schleife am Ende sparen?
            continue;
        }

        obj_state_t objstate;
        w1.state.GetObjState(id, objstate);
        obj = m_interpworld.GetObj(id);
        if(!obj)
        {
            obj = new CObj(&m_interpworld);
            obj->SetObjState(&objstate, id);
            m_interpworld.AddObj(obj);
        }
        else
        {
            obj->SetObjState(&objstate, id);
        }
    }
    // Objekte l�schen, die es jetzt nicht mehr gibt
    OBJITER deliter;
    for(deliter = m_interpworld.ObjBegin();deliter != m_interpworld.ObjEnd(); deliter++)
    {
        obj = (*deliter).second;
        if(!w1.state.ObjStateExists(obj->GetID()) ||
           !w2.state.ObjStateExists(obj->GetID()))
            m_interpworld.DelObj(obj->GetID());
    }

    m_interpworld.UpdatePendingObjs();
}

void CWorldInterp::Update(const float dt, const uint32_t ticks) // Interpoliert zwischen versch. world_state_t
{
    const uint32_t rendertime = ticks - RENDER_DELAY; // Zeitpunkt f�r den interpoliert werden soll
    const uint32_t updategap = state2.localtime - state1.localtime;

    if(updategap < 1)
        return;

    const float a = (float)(rendertime - state1.localtime);
    f = a/updategap;

    if(f > 1.0f)
    {
        return;
    }

    std::map<int, int>::iterator iter1, iter2;
    OBJITER iter;
    CObj* obj;
    vec3_t origin1, origin2, origin, vel1, vel2;
    obj_state_t obj1, obj2;
    for(iter = ObjBegin();iter != ObjEnd(); iter++)
    {
        obj = (*iter).second;
        assert(state1.state.ObjStateExists(obj->GetID()));
        assert(state2.state.ObjStateExists(obj->GetID())); // wenn das schief geht, muss gepr�ft werden, ob das objekt richtig in die interp world eingef�gt wurde
        state1.state.GetObjState(obj->GetID(), obj1);
        state2.state.GetObjState(obj->GetID(), obj2);

        origin1 = obj1.origin;
        origin2 = obj2.origin;
        vel1 = obj1.vel;
        vel2 = obj2.vel;

        const float dist_sqr = (origin1 - origin2).AbsSquared();
        if(dist_sqr > 0.3f*0.3f) // bei kurzen abst�nden linear, sonst mit hermite raumkurve
        {
            if(!vel1.IsNullEpsilon())
                vel1.Normalize();
            if(!vel2.IsNullEpsilon())
                vel2.Normalize();
            origin = vec3_t::Hermite(origin1, origin2, vel1, vel2, f);
        }
        else
        {
            origin = vec3_t::Lerp(origin1, origin2, f);
        }

        obj->SetOrigin(origin);
        obj->SetRot(quaternion_t(obj1.rot, obj2.rot, f)); // Quaternion Slerp
    }
}


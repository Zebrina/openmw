#include <components/debug/debuglog.hpp>

#include <components/sceneutil/positionattitudetransform.hpp>

#include <components/esm/loadcell.hpp>

#include <components/compiler/opcodes.hpp>

#include <components/interpreter/interpreter.hpp>
#include <components/interpreter/runtime.hpp>
#include <components/interpreter/opcodes.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"

#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/manualref.hpp"
#include "../mwworld/player.hpp"

#include "../mwmechanics/actorutil.hpp"

#include "interpretercontext.hpp"
#include "ref.hpp"

namespace MWScript
{
    namespace Transformation
    {
        void moveStandingActors(const MWWorld::Ptr &ptr, const osg::Vec3f& diff)
        {
            std::vector<MWWorld::Ptr> actors;
            MWBase::Environment::get().getWorld()->getActorsStandingOn (ptr, actors);
            for (auto& actor : actors)
                MWBase::Environment::get().getWorld()->moveObjectBy(actor, diff);
        }

        template<class R>
        class OpGetDistance : public Interpreter::Opcode0
        {
            public:

                void execute (Interpreter::Runtime& runtime) override
                {
                    MWWorld::Ptr from = R()(runtime, !R::implicit);
                    std::string name = runtime.getStringLiteral (runtime[0].mInteger);
                    runtime.pop();

                    if (from.isEmpty())
                    {
                        std::string error = "Missing implicit ref";
                        runtime.getContext().report(error);
                        Log(Debug::Error) << error;
                        runtime.push(0.f);
                        return;
                    }

                    if (from.getContainerStore()) // is the object contained?
                    {
                        MWWorld::Ptr container = MWBase::Environment::get().getWorld()->findContainer(from);

                        if (!container.isEmpty())
                            from = container;
                        else
                        {
                            std::string error = "Failed to find the container of object '" + from.getCellRef().getRefId() + "'";
                            runtime.getContext().report(error);
                            Log(Debug::Error) << error;
                            runtime.push(0.f);
                            return;
                        }
                    }

                    const MWWorld::Ptr to = MWBase::Environment::get().getWorld()->searchPtr(name, false);
                    if (to.isEmpty())
                    {
                        std::string error = "Failed to find an instance of object '" + name + "'";
                        runtime.getContext().report(error);
                        Log(Debug::Error) << error;
                        runtime.push(0.f);
                        return;
                    }

                    float distance;
                    // If the objects are in different worldspaces, return a large value (just like vanilla)
                    if (!to.isInCell() || !from.isInCell() || to.getCell()->getCell()->getCellId().mWorldspace != from.getCell()->getCell()->getCellId().mWorldspace)
                        distance = std::numeric_limits<float>::max();
                    else
                    {
                        double diff[3];

                        const float* const pos1 = to.getRefData().getPosition().pos;
                        const float* const pos2 = from.getRefData().getPosition().pos;
                        for (int i=0; i<3; ++i)
                            diff[i] = pos1[i] - pos2[i];

                        distance = static_cast<float>(std::sqrt(diff[0] * diff[0] + diff[1] * diff[1] + diff[2] * diff[2]));
                    }

                    runtime.push(distance);
                }
        };

        template<class R>
        class OpSetScale : public Interpreter::Opcode0
        {
            public:

                void execute (Interpreter::Runtime& runtime) override
                {
                    MWWorld::Ptr ptr = R()(runtime);

                    Interpreter::Type_Float scale = runtime[0].mFloat;
                    runtime.pop();

                    MWBase::Environment::get().getWorld()->scaleObject(ptr,scale);
                }
        };

        template<class R>
        class OpGetScale : public Interpreter::Opcode0
        {
            public:

                void execute (Interpreter::Runtime& runtime) override
                {
                    MWWorld::Ptr ptr = R()(runtime);
                    runtime.push(ptr.getCellRef().getScale());
                }
        };

        template<class R>
        class OpModScale : public Interpreter::Opcode0
        {
            public:

                void execute (Interpreter::Runtime& runtime) override
                {
                    MWWorld::Ptr ptr = R()(runtime);

                    Interpreter::Type_Float scale = runtime[0].mFloat;
                    runtime.pop();

                    // add the parameter to the object's scale.
                    MWBase::Environment::get().getWorld()->scaleObject(ptr,ptr.getCellRef().getScale() + scale);
                }
        };

        template<class R>
        class OpSetAngle : public Interpreter::Opcode0
        {
            public:

                void execute (Interpreter::Runtime& runtime) override
                {
                    MWWorld::Ptr ptr = R()(runtime);

                    std::string axis = runtime.getStringLiteral (runtime[0].mInteger);
                    runtime.pop();
                    Interpreter::Type_Float angle = osg::DegreesToRadians(runtime[0].mFloat);
                    runtime.pop();

                    float ax = ptr.getRefData().getPosition().rot[0];
                    float ay = ptr.getRefData().getPosition().rot[1];
                    float az = ptr.getRefData().getPosition().rot[2];

                    // XYZ axis use the inverse (XYZ) rotation order like vanilla SetAngle.
                    // UWV axis use the standard (ZYX) rotation order like TESCS/OpenMW-CS and the rest of the game.
                    if (axis == "x")
                        MWBase::Environment::get().getWorld()->rotateObject(ptr,osg::Vec3f(angle,ay,az),MWBase::RotationFlag_inverseOrder);
                    else if (axis == "y")
                        MWBase::Environment::get().getWorld()->rotateObject(ptr,osg::Vec3f(ax,angle,az),MWBase::RotationFlag_inverseOrder);
                    else if (axis == "z")
                        MWBase::Environment::get().getWorld()->rotateObject(ptr,osg::Vec3f(ax,ay,angle),MWBase::RotationFlag_inverseOrder);
                    else if (axis == "u")
                        MWBase::Environment::get().getWorld()->rotateObject(ptr,osg::Vec3f(angle,ay,az),MWBase::RotationFlag_none);
                    else if (axis == "w")
                        MWBase::Environment::get().getWorld()->rotateObject(ptr,osg::Vec3f(ax,angle,az),MWBase::RotationFlag_none);
                    else if (axis == "v")
                        MWBase::Environment::get().getWorld()->rotateObject(ptr,osg::Vec3f(ax,ay,angle),MWBase::RotationFlag_none);
                }
        };

        template<class R>
        class OpGetStartingAngle : public Interpreter::Opcode0
        {
            public:

                void execute (Interpreter::Runtime& runtime) override
                {
                    MWWorld::Ptr ptr = R()(runtime);

                    std::string axis = runtime.getStringLiteral (runtime[0].mInteger);
                    runtime.pop();

                    if (axis == "x")
                    {
                        runtime.push(osg::RadiansToDegrees(ptr.getCellRef().getPosition().rot[0]));
                    }
                    else if (axis == "y")
                    {
                        runtime.push(osg::RadiansToDegrees(ptr.getCellRef().getPosition().rot[1]));
                    }
                    else if (axis == "z")
                    {
                        runtime.push(osg::RadiansToDegrees(ptr.getCellRef().getPosition().rot[2]));
                    }
                }
        };

        template<class R>
        class OpGetAngle : public Interpreter::Opcode0
        {
            public:

                void execute (Interpreter::Runtime& runtime) override
                {
                    MWWorld::Ptr ptr = R()(runtime);

                    std::string axis = runtime.getStringLiteral (runtime[0].mInteger);
                    runtime.pop();

                    if (axis=="x")
                    {
                        runtime.push(osg::RadiansToDegrees(ptr.getRefData().getPosition().rot[0]));
                    }
                    else if (axis=="y")
                    {
                        runtime.push(osg::RadiansToDegrees(ptr.getRefData().getPosition().rot[1]));
                    }
                    else if (axis=="z")
                    {
                        runtime.push(osg::RadiansToDegrees(ptr.getRefData().getPosition().rot[2]));
                    }
                }
        };

        template<class R>
        class OpGetPos : public Interpreter::Opcode0
        {
            public:

                void execute (Interpreter::Runtime& runtime) override
                {
                    MWWorld::Ptr ptr = R()(runtime);

                    std::string axis = runtime.getStringLiteral (runtime[0].mInteger);
                    runtime.pop();

                    if(axis == "x")
                    {
                        runtime.push(ptr.getRefData().getPosition().pos[0]);
                    }
                    else if(axis == "y")
                    {
                        runtime.push(ptr.getRefData().getPosition().pos[1]);
                    }
                    else if(axis == "z")
                    {
                        runtime.push(ptr.getRefData().getPosition().pos[2]);
                    }
                }
        };

        template<class R>
        class OpSetPos : public Interpreter::Opcode0
        {
            public:

                void execute (Interpreter::Runtime& runtime) override
                {
                    MWWorld::Ptr ptr = R()(runtime);

                    if (!ptr.isInCell())
                        return;

                    std::string axis = runtime.getStringLiteral (runtime[0].mInteger);
                    runtime.pop();
                    Interpreter::Type_Float pos = runtime[0].mFloat;
                    runtime.pop();

                    // Note: SetPos does not skip weather transitions in vanilla engine, so we do not call setTeleported(true) here.

                    const auto curPos = ptr.getRefData().getPosition().asVec3();
                    auto newPos = curPos;
                    if(axis == "x")
                    {
                        newPos[0] = pos;
                    }
                    else if(axis == "y")
                    {
                        newPos[1] = pos;
                    }
                    else if(axis == "z")
                    {
                        // We should not place actors under ground
                        if (ptr.getClass().isActor())
                        {
                            float terrainHeight = -std::numeric_limits<float>::max();
                            if (ptr.getCell()->isExterior())
                                terrainHeight = MWBase::Environment::get().getWorld()->getTerrainHeightAt(curPos);
 
                            if (pos < terrainHeight)
                                pos = terrainHeight;
                        }
 
                        newPos[2] = pos;
                    }
                    else
                    {
                        return;
                    }

                    dynamic_cast<MWScript::InterpreterContext&>(runtime.getContext()).updatePtr(ptr,
                        MWBase::Environment::get().getWorld()->moveObject(ptr, newPos, true, true));
                }
        };

        template<class R>
        class OpGetStartingPos : public Interpreter::Opcode0
        {
            public:

                void execute (Interpreter::Runtime& runtime) override
                {
                    MWWorld::Ptr ptr = R()(runtime);

                    std::string axis = runtime.getStringLiteral (runtime[0].mInteger);
                    runtime.pop();

                    if(axis == "x")
                    {
                        runtime.push(ptr.getCellRef().getPosition().pos[0]);
                    }
                    else if(axis == "y")
                    {
                        runtime.push(ptr.getCellRef().getPosition().pos[1]);
                    }
                    else if(axis == "z")
                    {
                        runtime.push(ptr.getCellRef().getPosition().pos[2]);
                    }
                }
        };

        template<class R>
        class OpPositionCell : public Interpreter::Opcode0
        {
            public:

                void execute (Interpreter::Runtime& runtime) override
                {
                    MWWorld::Ptr ptr = R()(runtime);

                    if (ptr.getContainerStore())
                        return;

                    bool isPlayer = ptr == MWMechanics::getPlayer();
                    if (isPlayer)
                    {
                        MWBase::Environment::get().getWorld()->getPlayer().setTeleported(true);
                    }

                    Interpreter::Type_Float x = runtime[0].mFloat;
                    runtime.pop();
                    Interpreter::Type_Float y = runtime[0].mFloat;
                    runtime.pop();
                    Interpreter::Type_Float z = runtime[0].mFloat;
                    runtime.pop();
                    Interpreter::Type_Float zRot = runtime[0].mFloat;
                    runtime.pop();
                    std::string cellID = runtime.getStringLiteral (runtime[0].mInteger);
                    runtime.pop();

                    MWWorld::CellStore* store = nullptr;
                    try
                    {
                        store = MWBase::Environment::get().getWorld()->getInterior(cellID);
                    }
                    catch(std::exception&)
                    {
                        // cell not found, move to exterior instead if moving the player (vanilla PositionCell compatibility)
                        const ESM::Cell* cell = MWBase::Environment::get().getWorld()->getExterior(cellID);
                        if(!cell)
                        {
                            std::string error = "Warning: PositionCell: unknown interior cell (" + cellID + ")";
                            if(isPlayer)
                                error += ", moving to exterior instead";
                            runtime.getContext().report (error);
                            Log(Debug::Warning) << error;
                            if(!isPlayer)
                                return;
                        }
                        int cx,cy;
                        MWBase::Environment::get().getWorld()->positionToIndex(x,y,cx,cy);
                        store = MWBase::Environment::get().getWorld()->getExterior(cx,cy);
                    }
                    if(store)
                    {
                        MWWorld::Ptr base = ptr;
                        ptr = MWBase::Environment::get().getWorld()->moveObject(ptr,store,osg::Vec3f(x,y,z));
                        dynamic_cast<MWScript::InterpreterContext&>(runtime.getContext()).updatePtr(base,ptr);

                        auto rot = ptr.getRefData().getPosition().asRotationVec3();
                        // Note that you must specify ZRot in minutes (1 degree = 60 minutes; north = 0, east = 5400, south = 10800, west = 16200)
                        // except for when you position the player, then degrees must be used.
                        // See "Morrowind Scripting for Dummies (9th Edition)" pages 50 and 54 for reference.
                        if(!isPlayer)
                            zRot = zRot/60.0f;
                        rot.z() = osg::DegreesToRadians(zRot);
                        MWBase::Environment::get().getWorld()->rotateObject(ptr,rot);

                        ptr.getClass().adjustPosition(ptr, false);
                    }
                }
        };

        template<class R>
        class OpPosition : public Interpreter::Opcode0
        {
            public:

                void execute (Interpreter::Runtime& runtime) override
                {
                    MWWorld::Ptr ptr = R()(runtime);

                    if (!ptr.isInCell())
                        return;

                    if (ptr == MWMechanics::getPlayer())
                    {
                        MWBase::Environment::get().getWorld()->getPlayer().setTeleported(true);
                    }

                    Interpreter::Type_Float x = runtime[0].mFloat;
                    runtime.pop();
                    Interpreter::Type_Float y = runtime[0].mFloat;
                    runtime.pop();
                    Interpreter::Type_Float z = runtime[0].mFloat;
                    runtime.pop();
                    Interpreter::Type_Float zRot = runtime[0].mFloat;
                    runtime.pop();
                    int cx,cy;
                    MWBase::Environment::get().getWorld()->positionToIndex(x,y,cx,cy);

                    // another morrowind oddity: player will be moved to the exterior cell at this location,
                    // non-player actors will move within the cell they are in.
                    MWWorld::Ptr base = ptr;
                    if (ptr == MWMechanics::getPlayer())
                    {
                        MWWorld::CellStore* cell = MWBase::Environment::get().getWorld()->getExterior(cx,cy);
                        ptr = MWBase::Environment::get().getWorld()->moveObject(ptr, cell, osg::Vec3(x, y, z));
                    }
                    else
                    {
                        ptr = MWBase::Environment::get().getWorld()->moveObject(ptr, osg::Vec3f(x, y, z), true, true);
                    }
                    dynamic_cast<MWScript::InterpreterContext&>(runtime.getContext()).updatePtr(base,ptr);

                    auto rot = ptr.getRefData().getPosition().asRotationVec3();
                    // Note that you must specify ZRot in minutes (1 degree = 60 minutes; north = 0, east = 5400, south = 10800, west = 16200)
                    // except for when you position the player, then degrees must be used.
                    // See "Morrowind Scripting for Dummies (9th Edition)" pages 50 and 54 for reference.
                    if(ptr != MWMechanics::getPlayer())
                        zRot = zRot/60.0f;
                    rot.z() = osg::DegreesToRadians(zRot);
                    MWBase::Environment::get().getWorld()->rotateObject(ptr,rot);
                    ptr.getClass().adjustPosition(ptr, false);
                }
        };

        class OpPlaceItemCell : public Interpreter::Opcode0
        {
            public:

                void execute (Interpreter::Runtime& runtime) override
                {
                    std::string itemID = runtime.getStringLiteral (runtime[0].mInteger);
                    runtime.pop();
                    std::string cellID = runtime.getStringLiteral (runtime[0].mInteger);
                    runtime.pop();

                    Interpreter::Type_Float x = runtime[0].mFloat;
                    runtime.pop();
                    Interpreter::Type_Float y = runtime[0].mFloat;
                    runtime.pop();
                    Interpreter::Type_Float z = runtime[0].mFloat;
                    runtime.pop();
                    Interpreter::Type_Float zRotDegrees = runtime[0].mFloat;
                    runtime.pop();

                    MWWorld::CellStore* store = nullptr;
                    try
                    {
                        store = MWBase::Environment::get().getWorld()->getInterior(cellID);
                    }
                    catch(std::exception&)
                    {
                        const ESM::Cell* cell = MWBase::Environment::get().getWorld()->getExterior(cellID);
                        int cx,cy;
                        MWBase::Environment::get().getWorld()->positionToIndex(x,y,cx,cy);
                        store = MWBase::Environment::get().getWorld()->getExterior(cx,cy);
                        if(!cell)
                        {
                            runtime.getContext().report ("unknown cell (" + cellID + ")");
                            Log(Debug::Error) << "Error: unknown cell (" << cellID << ")";
                        }
                    }
                    if(store)
                    {
                        ESM::Position pos;
                        pos.pos[0] = x;
                        pos.pos[1] = y;
                        pos.pos[2] = z;
                        pos.rot[0] = pos.rot[1] = 0;
                        pos.rot[2] = osg::DegreesToRadians(zRotDegrees);
                        MWWorld::ManualRef ref(MWBase::Environment::get().getWorld()->getStore(),itemID);
                        ref.getPtr().mRef->mData.mPhysicsPostponed = !ref.getPtr().getClass().isActor();
                        ref.getPtr().getCellRef().setPosition(pos);
                        MWWorld::Ptr placed = MWBase::Environment::get().getWorld()->placeObject(ref.getPtr(),store,pos);
                        placed.getClass().adjustPosition(placed, true);
                    }
                }
        };

        class OpPlaceItem : public Interpreter::Opcode0
        {
            public:

                void execute (Interpreter::Runtime& runtime) override
                {
                    std::string itemID = runtime.getStringLiteral (runtime[0].mInteger);
                    runtime.pop();

                    Interpreter::Type_Float x = runtime[0].mFloat;
                    runtime.pop();
                    Interpreter::Type_Float y = runtime[0].mFloat;
                    runtime.pop();
                    Interpreter::Type_Float z = runtime[0].mFloat;
                    runtime.pop();
                    Interpreter::Type_Float zRotDegrees = runtime[0].mFloat;
                    runtime.pop();

                    MWWorld::Ptr player = MWMechanics::getPlayer();

                    if (!player.isInCell())
                        throw std::runtime_error("player not in a cell");

                    MWWorld::CellStore* store = nullptr;
                    if (player.getCell()->isExterior())
                    {
                        int cx,cy;
                        MWBase::Environment::get().getWorld()->positionToIndex(x,y,cx,cy);
                        store = MWBase::Environment::get().getWorld()->getExterior(cx,cy);
                    }
                    else
                        store = player.getCell();

                    ESM::Position pos;
                    pos.pos[0] = x;
                    pos.pos[1] = y;
                    pos.pos[2] = z;
                    pos.rot[0] = pos.rot[1] = 0;
                    pos.rot[2] = osg::DegreesToRadians(zRotDegrees);
                    MWWorld::ManualRef ref(MWBase::Environment::get().getWorld()->getStore(),itemID);
                    ref.getPtr().mRef->mData.mPhysicsPostponed = !ref.getPtr().getClass().isActor();
                    ref.getPtr().getCellRef().setPosition(pos);
                    MWWorld::Ptr placed = MWBase::Environment::get().getWorld()->placeObject(ref.getPtr(),store,pos);
                    placed.getClass().adjustPosition(placed, true);
                }
        };

        template<class R, bool pc>
        class OpPlaceAt : public Interpreter::Opcode0
        {
            public:

                void execute (Interpreter::Runtime& runtime) override
                {
                    MWWorld::Ptr actor = pc
                        ? MWMechanics::getPlayer()
                        : R()(runtime);

                    std::string itemID = runtime.getStringLiteral (runtime[0].mInteger);
                    runtime.pop();

                    Interpreter::Type_Integer count = runtime[0].mInteger;
                    runtime.pop();
                    Interpreter::Type_Float distance = runtime[0].mFloat;
                    runtime.pop();
                    Interpreter::Type_Integer direction = runtime[0].mInteger;
                    runtime.pop();

                    if (direction < 0 || direction > 3)
                        throw std::runtime_error ("invalid direction");

                    if (count<0)
                        throw std::runtime_error ("count must be non-negative");

                    if (!actor.isInCell())
                        throw std::runtime_error ("actor is not in a cell");

                    for (int i=0; i<count; ++i)
                    {
                        // create item
                        MWWorld::ManualRef ref(MWBase::Environment::get().getWorld()->getStore(), itemID, 1);
                        ref.getPtr().mRef->mData.mPhysicsPostponed = !ref.getPtr().getClass().isActor();

                        MWWorld::Ptr ptr = MWBase::Environment::get().getWorld()->safePlaceObject(ref.getPtr(), actor, actor.getCell(), direction, distance);
                        MWBase::Environment::get().getWorld()->scaleObject(ptr, actor.getCellRef().getScale());
                    }
                }
        };

        template<class R>
        class OpRotate : public Interpreter::Opcode0
        {
            public:

                void execute (Interpreter::Runtime& runtime) override
                {
                    const MWWorld::Ptr& ptr = R()(runtime);

                    std::string axis = runtime.getStringLiteral (runtime[0].mInteger);
                    runtime.pop();
                    Interpreter::Type_Float rotation = osg::DegreesToRadians(runtime[0].mFloat*MWBase::Environment::get().getFrameDuration());
                    runtime.pop();

                    auto rot = ptr.getRefData().getPosition().asRotationVec3();
                    // Regardless of the axis argument, the player may only be rotated on Z
                    if (axis == "z" || MWMechanics::getPlayer() == ptr)
                        rot.z() += rotation;
                    else if (axis == "x")
                        rot.x() += rotation;
                    else if (axis == "y")
                        rot.y() += rotation;
                    MWBase::Environment::get().getWorld()->rotateObject(ptr,rot);
                }
        };

        template<class R>
        class OpRotateWorld : public Interpreter::Opcode0
        {
            public:

                void execute (Interpreter::Runtime& runtime) override
                {
                    MWWorld::Ptr ptr = R()(runtime);

                    std::string axis = runtime.getStringLiteral (runtime[0].mInteger);
                    runtime.pop();
                    Interpreter::Type_Float rotation = osg::DegreesToRadians(runtime[0].mFloat*MWBase::Environment::get().getFrameDuration());
                    runtime.pop();

                    if (!ptr.getRefData().getBaseNode())
                        return;

                    // We can rotate actors only around Z axis
                    if (ptr.getClass().isActor() && (axis == "x" || axis == "y"))
                        return;

                    osg::Quat rot;
                    if (axis == "x")
                        rot = osg::Quat(rotation, -osg::X_AXIS);
                    else if (axis == "y")
                        rot = osg::Quat(rotation, -osg::Y_AXIS);
                    else if (axis == "z")
                        rot = osg::Quat(rotation, -osg::Z_AXIS);
                    else
                        return;

                    osg::Quat attitude = ptr.getRefData().getBaseNode()->getAttitude();
                    MWBase::Environment::get().getWorld()->rotateWorldObject(ptr, attitude * rot);
                }
        };

        template<class R>
        class OpSetAtStart : public Interpreter::Opcode0
        {
            public:

                void execute (Interpreter::Runtime& runtime) override
                {
                    MWWorld::Ptr ptr = R()(runtime);

                    if (!ptr.isInCell())
                        return;

                    MWBase::Environment::get().getWorld()->rotateObject(ptr, ptr.getCellRef().getPosition().asRotationVec3());

                    dynamic_cast<MWScript::InterpreterContext&>(runtime.getContext()).updatePtr(ptr,
                        MWBase::Environment::get().getWorld()->moveObject(ptr, ptr.getCellRef().getPosition().asVec3()));

                }
        };

        template<class R>
        class OpMove : public Interpreter::Opcode0
        {
            public:

                void execute (Interpreter::Runtime& runtime) override
                {
                    const MWWorld::Ptr& ptr = R()(runtime);

                    if (!ptr.isInCell())
                        return;

                    std::string axis = runtime.getStringLiteral (runtime[0].mInteger);
                    runtime.pop();
                    Interpreter::Type_Float movement = (runtime[0].mFloat*MWBase::Environment::get().getFrameDuration());
                    runtime.pop();

                    osg::Vec3f posChange;
                    if (axis == "x")
                    {
                        posChange=osg::Vec3f(movement, 0, 0);
                    }
                    else if (axis == "y")
                    {
                        posChange=osg::Vec3f(0, movement, 0);
                    }
                    else if (axis == "z")
                    {
                        posChange=osg::Vec3f(0, 0, movement);
                    }
                    else
                        return;

                    // is it correct that disabled objects can't be Move-d?
                    if (!ptr.getRefData().getBaseNode())
                        return;

                    osg::Vec3f diff = ptr.getRefData().getBaseNode()->getAttitude() * posChange;

                    // We should move actors, standing on moving object, too.
                    // This approach can be used to create elevators.
                    moveStandingActors(ptr, diff);
                    dynamic_cast<MWScript::InterpreterContext&>(runtime.getContext()).updatePtr(ptr,
                        MWBase::Environment::get().getWorld()->moveObjectBy(ptr, diff));
                }
        };

        template<class R>
        class OpMoveWorld : public Interpreter::Opcode0
        {
            public:

                void execute (Interpreter::Runtime& runtime) override
                {
                    MWWorld::Ptr ptr = R()(runtime);

                    if (!ptr.isInCell())
                        return;

                    std::string axis = runtime.getStringLiteral (runtime[0].mInteger);
                    runtime.pop();
                    Interpreter::Type_Float movement = (runtime[0].mFloat*MWBase::Environment::get().getFrameDuration());
                    runtime.pop();

                    osg::Vec3f diff;

                    if (axis == "x")
                        diff.x() = movement;
                    else if (axis == "y")
                        diff.y() = movement;
                    else if (axis == "z")
                        diff.z() = movement;
                    else
                        return;

                    // We should move actors, standing on moving object, too.
                    // This approach can be used to create elevators.
                    moveStandingActors(ptr, diff);
                    dynamic_cast<MWScript::InterpreterContext&>(runtime.getContext()).updatePtr(ptr,
                        MWBase::Environment::get().getWorld()->moveObjectBy(ptr, diff));
                }
        };

        class OpResetActors : public Interpreter::Opcode0
        {
        public:

            void execute (Interpreter::Runtime& runtime) override
            {
                MWBase::Environment::get().getWorld()->resetActors();
            }
        };

        class OpFixme : public Interpreter::Opcode0
        {
        public:

            void execute (Interpreter::Runtime& runtime) override
            {
                MWBase::Environment::get().getWorld()->fixPosition();
            }
        };

        void installOpcodes (Interpreter::Interpreter& interpreter)
        {
            interpreter.installSegment5(Compiler::Transformation::opcodeGetDistance, new OpGetDistance<ImplicitRef>);
            interpreter.installSegment5(Compiler::Transformation::opcodeGetDistanceExplicit, new OpGetDistance<ExplicitRef>);
            interpreter.installSegment5(Compiler::Transformation::opcodeSetScale,new OpSetScale<ImplicitRef>);
            interpreter.installSegment5(Compiler::Transformation::opcodeSetScaleExplicit,new OpSetScale<ExplicitRef>);
            interpreter.installSegment5(Compiler::Transformation::opcodeSetAngle,new OpSetAngle<ImplicitRef>);
            interpreter.installSegment5(Compiler::Transformation::opcodeSetAngleExplicit,new OpSetAngle<ExplicitRef>);
            interpreter.installSegment5(Compiler::Transformation::opcodeGetScale,new OpGetScale<ImplicitRef>);
            interpreter.installSegment5(Compiler::Transformation::opcodeGetScaleExplicit,new OpGetScale<ExplicitRef>);
            interpreter.installSegment5(Compiler::Transformation::opcodeGetAngle,new OpGetAngle<ImplicitRef>);
            interpreter.installSegment5(Compiler::Transformation::opcodeGetAngleExplicit,new OpGetAngle<ExplicitRef>);
            interpreter.installSegment5(Compiler::Transformation::opcodeGetPos,new OpGetPos<ImplicitRef>);
            interpreter.installSegment5(Compiler::Transformation::opcodeGetPosExplicit,new OpGetPos<ExplicitRef>);
            interpreter.installSegment5(Compiler::Transformation::opcodeSetPos,new OpSetPos<ImplicitRef>);
            interpreter.installSegment5(Compiler::Transformation::opcodeSetPosExplicit,new OpSetPos<ExplicitRef>);
            interpreter.installSegment5(Compiler::Transformation::opcodeGetStartingPos,new OpGetStartingPos<ImplicitRef>);
            interpreter.installSegment5(Compiler::Transformation::opcodeGetStartingPosExplicit,new OpGetStartingPos<ExplicitRef>);
            interpreter.installSegment5(Compiler::Transformation::opcodePosition,new OpPosition<ImplicitRef>);
            interpreter.installSegment5(Compiler::Transformation::opcodePositionExplicit,new OpPosition<ExplicitRef>);
            interpreter.installSegment5(Compiler::Transformation::opcodePositionCell,new OpPositionCell<ImplicitRef>);
            interpreter.installSegment5(Compiler::Transformation::opcodePositionCellExplicit,new OpPositionCell<ExplicitRef>);
            interpreter.installSegment5(Compiler::Transformation::opcodePlaceItemCell,new OpPlaceItemCell);
            interpreter.installSegment5(Compiler::Transformation::opcodePlaceItem,new OpPlaceItem);
            interpreter.installSegment5(Compiler::Transformation::opcodePlaceAtPc,new OpPlaceAt<ImplicitRef, true>);
            interpreter.installSegment5(Compiler::Transformation::opcodePlaceAtMe,new OpPlaceAt<ImplicitRef, false>);
            interpreter.installSegment5(Compiler::Transformation::opcodePlaceAtMeExplicit,new OpPlaceAt<ExplicitRef, false>);
            interpreter.installSegment5(Compiler::Transformation::opcodeModScale,new OpModScale<ImplicitRef>);
            interpreter.installSegment5(Compiler::Transformation::opcodeModScaleExplicit,new OpModScale<ExplicitRef>);
            interpreter.installSegment5(Compiler::Transformation::opcodeRotate,new OpRotate<ImplicitRef>);
            interpreter.installSegment5(Compiler::Transformation::opcodeRotateExplicit,new OpRotate<ExplicitRef>);
            interpreter.installSegment5(Compiler::Transformation::opcodeRotateWorld,new OpRotateWorld<ImplicitRef>);
            interpreter.installSegment5(Compiler::Transformation::opcodeRotateWorldExplicit,new OpRotateWorld<ExplicitRef>);
            interpreter.installSegment5(Compiler::Transformation::opcodeSetAtStart,new OpSetAtStart<ImplicitRef>);
            interpreter.installSegment5(Compiler::Transformation::opcodeSetAtStartExplicit,new OpSetAtStart<ExplicitRef>);
            interpreter.installSegment5(Compiler::Transformation::opcodeMove,new OpMove<ImplicitRef>);
            interpreter.installSegment5(Compiler::Transformation::opcodeMoveExplicit,new OpMove<ExplicitRef>);
            interpreter.installSegment5(Compiler::Transformation::opcodeMoveWorld,new OpMoveWorld<ImplicitRef>);
            interpreter.installSegment5(Compiler::Transformation::opcodeMoveWorldExplicit,new OpMoveWorld<ExplicitRef>);
            interpreter.installSegment5(Compiler::Transformation::opcodeGetStartingAngle, new OpGetStartingAngle<ImplicitRef>);
            interpreter.installSegment5(Compiler::Transformation::opcodeGetStartingAngleExplicit, new OpGetStartingAngle<ExplicitRef>);
            interpreter.installSegment5(Compiler::Transformation::opcodeResetActors, new OpResetActors);
            interpreter.installSegment5(Compiler::Transformation::opcodeFixme, new OpFixme);
        }
    }
}

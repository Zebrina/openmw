#include "actoradapter.hpp"

#include <components/debug/debuglog.hpp>
#include <components/esm/loadarmo.hpp>
#include <components/esm/loadclot.hpp>
#include <components/esm/loadnpc.hpp>
#include <components/esm/loadrace.hpp>
#include <components/esm/mappings.hpp>

#include "data.hpp"

namespace CSMWorld
{
    const std::string& ActorAdapter::RaceData::getId() const
    {
        return mId;
    }

    bool ActorAdapter::RaceData::handlesPart(ESM::PartReferenceType type) const
    {
        switch (type)
        {
            case ESM::PRT_Skirt:
            case ESM::PRT_Shield:
            case ESM::PRT_RPauldron:
            case ESM::PRT_LPauldron:
            case ESM::PRT_Weapon:
                return false;
            default:
                return true;
        }
    }

    const std::string& ActorAdapter::RaceData::getFemalePart(ESM::PartReferenceType index) const
    {
        return mFemaleParts[ESM::getMeshPart(index)];
    }

    const std::string& ActorAdapter::RaceData::getMalePart(ESM::PartReferenceType index) const
    {
        return mMaleParts[ESM::getMeshPart(index)];
    }

    bool ActorAdapter::RaceData::hasDependency(const std::string& id) const
    {
        return mDependencies.find(id) != mDependencies.end();
    }

    void ActorAdapter::RaceData::setFemalePart(ESM::BodyPart::MeshPart index, const std::string& partId)
    {
        mFemaleParts[index] = partId;
        addOtherDependency(partId);
    }

    void ActorAdapter::RaceData::setMalePart(ESM::BodyPart::MeshPart index, const std::string& partId)
    {
        mMaleParts[index] = partId;
        addOtherDependency(partId);
    }

    void ActorAdapter::RaceData::addOtherDependency(const std::string& id)
    {
        if (!id.empty()) mDependencies.emplace(id);
    }

    void ActorAdapter::RaceData::reset(const std::string& id)
    {
        mId = id;
        for (auto& str : mFemaleParts)
            str.clear();
        for (auto& str : mMaleParts)
            str.clear();
        mDependencies.clear();

        // Mark self as a dependency
        addOtherDependency(id);
    }


    const std::string& ActorAdapter::ActorData::getId() const
    {
        return mId;
    }

    bool ActorAdapter::ActorData::isFemale() const
    {
        return mFemale;
    }

    const std::string& ActorAdapter::ActorData::getPart(ESM::PartReferenceType index) const
    {
        if (mParts[index].empty() && mRaceData && mRaceData->handlesPart(index))
        {
            return mFemale ? mRaceData->getFemalePart(index) : mRaceData->getMalePart(index);
        }
        return mParts[index];
    }

    bool ActorAdapter::ActorData::hasDependency(const std::string& id) const
    {
        return mDependencies.find(id) != mDependencies.end();
    }

    void ActorAdapter::ActorData::setPart(ESM::PartReferenceType index, const std::string& partId)
    {
        mParts[index] = partId;
        addOtherDependency(partId);
    }

    void ActorAdapter::ActorData::addOtherDependency(const std::string& id)
    {
        if (!id.empty()) mDependencies.emplace(id);
    }

    void ActorAdapter::ActorData::reset(const std::string& id, bool isFemale, RaceDataPtr raceData)
    {
        mId = id;
        mFemale = isFemale;
        mRaceData = raceData;
        for (auto& str : mParts)
            str.clear();
        mDependencies.clear();

        // Mark self and race as a dependency
        addOtherDependency(id);
        if (raceData) addOtherDependency(raceData->getId());
    }


    ActorAdapter::ActorAdapter(Data& data)
        : mReferenceables(data.getReferenceables())
        , mRaces(data.getRaces())
        , mBodyParts(data.getBodyParts())
    {
        // Setup qt slots and signals
        QAbstractItemModel* refModel = data.getTableModel(UniversalId::Type_Referenceable);
        connect(refModel, SIGNAL(rowsInserted(const QModelIndex&, int, int)),
                this, SLOT(handleReferenceablesInserted(const QModelIndex&, int, int)));
        connect(refModel, SIGNAL(dataChanged(const QModelIndex&, const QModelIndex&)),
                this, SLOT(handleReferenceableChanged(const QModelIndex&, const QModelIndex&)));
        connect(refModel, SIGNAL(rowsAboutToBeRemoved(const QModelIndex&, int, int)),
                this, SLOT(handleReferenceablesAboutToBeRemoved(const QModelIndex&, int, int)));

        QAbstractItemModel* raceModel = data.getTableModel(UniversalId::Type_Race);
        connect(raceModel, SIGNAL(rowsInserted(const QModelIndex&, int, int)),
                this, SLOT(handleRacesAboutToBeRemoved(const QModelIndex&, int, int)));
        connect(raceModel, SIGNAL(dataChanged(const QModelIndex&, const QModelIndex&)),
                this, SLOT(handleRaceChanged(const QModelIndex&, const QModelIndex&)));
        connect(raceModel, SIGNAL(rowsAboutToBeRemoved(const QModelIndex&, int, int)),
                this, SLOT(handleRacesAboutToBeRemoved(const QModelIndex&, int, int)));

        QAbstractItemModel* partModel = data.getTableModel(UniversalId::Type_BodyPart);
        connect(partModel, SIGNAL(rowsInserted(const QModelIndex&, int, int)),
                this, SLOT(handleBodyPartsInserted(const QModelIndex&, int, int)));
        connect(partModel, SIGNAL(dataChanged(const QModelIndex&, const QModelIndex&)),
                this, SLOT(handleBodyPartChanged(const QModelIndex&, const QModelIndex&)));
        connect(partModel, SIGNAL(rowsAboutToBeRemoved(const QModelIndex&, int, int)),
                this, SLOT(handleBodyPartsAboutToBeRemoved(const QModelIndex&, int, int)));
    }

    ActorAdapter::ActorDataPtr ActorAdapter::getActorData(const std::string& id)
    {
        // Return cached actor data if it exists
        ActorDataPtr data = mCachedActors.get(id);
        if (data)
        {
            return data;
        }

        // Create the actor data
        data.reset(new ActorData());
        setupActor(id, data);
        mCachedActors.insert(id, data);
        return data;
    }

    void ActorAdapter::handleReferenceablesInserted(const QModelIndex& parent, int start, int end)
    {
        // Only rows added at the top level are pertinent. Others are caught by dataChanged handler.
        if (!parent.isValid())
        {
            for (int row = start; row <= end; ++row)
            {
                std::string refId = mReferenceables.getId(row);
                markDirtyDependency(refId);
            }
        }

        // Update affected
        updateDirty();
    }

    void ActorAdapter::handleReferenceableChanged(const QModelIndex& topLeft, const QModelIndex& botRight)
    {
        int start = getHighestIndex(topLeft).row();
        int end = getHighestIndex(botRight).row();

        // A change to record status (ex. Deleted) returns an invalid botRight
        if (end == -1)
            end = start;

        // Handle each record
        for (int row = start; row <= end; ++row)
        {
            std::string refId = mReferenceables.getId(row);
            markDirtyDependency(refId);
        }

        // Update affected
        updateDirty();
    }

    void ActorAdapter::handleReferenceablesAboutToBeRemoved(const QModelIndex& parent, int start, int end)
    {
        // Only rows at the top are pertinent.
        if (!parent.isValid())
        {
            for (int row = start; row <= end; ++row)
            {
                std::string refId = mReferenceables.getId(row);
                markDirtyDependency(refId);
            }
        }
    }

    void ActorAdapter::handleReferenceablesRemoved(const QModelIndex& parent, int start, int end)
    {
        // Changes specified in handleReferenceablesAboutToBeRemoved
        updateDirty();
    }

    void ActorAdapter::handleRacesInserted(const QModelIndex& parent, int start, int end)
    {
        // Only rows added at the top are pertinent.
        if (!parent.isValid())
        {
            for (int row = start; row <= end; ++row)
            {
                std::string raceId = mReferenceables.getId(row);
                markDirtyDependency(raceId);
            }
        }

        // Update affected
        updateDirty();
    }

    void ActorAdapter::handleRaceChanged(const QModelIndex& topLeft, const QModelIndex& botRight)
    {
        int start = getHighestIndex(topLeft).row();
        int end = getHighestIndex(botRight).row();

        // A change to record status (ex. Deleted) returns an invalid botRight
        if (end == -1)
            end = start;

        for (int row = start; row <= end; ++row)
        {
            std::string raceId = mRaces.getId(row);
            markDirtyDependency(raceId);
        }

        // Update affected
        updateDirty();
    }

    void ActorAdapter::handleRacesAboutToBeRemoved(const QModelIndex& parent, int start, int end)
    {
        // Only changes at the top are pertinent.
        if (!parent.isValid())
        {
            for (int row = start; row <= end; ++row)
            {
                std::string raceId = mRaces.getId(row);
                markDirtyDependency(raceId);
            }
        }
    }

    void ActorAdapter::handleRacesRemoved(const QModelIndex& parent, int start, int end)
    {
        // Changes specified in handleRacesAboutToBeRemoved
        updateDirty();
    }

    void ActorAdapter::handleBodyPartsInserted(const QModelIndex& parent, int start, int end)
    {
        // Only rows added at the top are pertinent.
        if (!parent.isValid())
        {
            for (int row = start; row <= end; ++row)
            {
                // Race specified by part may need update
                auto& record = mBodyParts.getRecord(row);
                if (!record.isDeleted())
                {
                    markDirtyDependency(record.get().mRace);
                }

                std::string partId = mBodyParts.getId(row);
                markDirtyDependency(partId);
            }
        }

        // Update affected
        updateDirty();
    }

    void ActorAdapter::handleBodyPartChanged(const QModelIndex& topLeft, const QModelIndex& botRight)
    {
        int start = getHighestIndex(topLeft).row();
        int end = getHighestIndex(botRight).row();

        // A change to record status (ex. Deleted) returns an invalid botRight
        if (end == -1)
            end = start;

        for (int row = start; row <= end; ++row)
        {
            // Race specified by part may need update
            auto& record = mBodyParts.getRecord(row);
            if (!record.isDeleted())
            {
                markDirtyDependency(record.get().mRace);
            }

            // Update entries with a tracked dependency
            std::string partId = mBodyParts.getId(row);
            markDirtyDependency(partId);
        }

        // Update affected
        updateDirty();
    }

    void ActorAdapter::handleBodyPartsAboutToBeRemoved(const QModelIndex& parent, int start, int end)
    {
        // Only changes at the top are pertinent.
        if (!parent.isValid())
        {
            for (int row = start; row <= end; ++row)
            {
                std::string partId = mBodyParts.getId(row);
                markDirtyDependency(partId);
            }
        }
    }

    void ActorAdapter::handleBodyPartsRemoved(const QModelIndex& parent, int start, int end)
    {
        // Changes specified in handleBodyPartsAboutToBeRemoved
        updateDirty();
    }

    QModelIndex ActorAdapter::getHighestIndex(QModelIndex index) const
    {
        while (index.parent().isValid())
            index = index.parent();
        return index;
    }

    bool ActorAdapter::is1stPersonPart(const std::string& name) const
    {
        return name.size() >= 4 && name.find(".1st", name.size() - 4) != std::string::npos;
    }

    ActorAdapter::RaceDataPtr ActorAdapter::getRaceData(const std::string& id)
    {
        // Return cached race data if it exists
        RaceDataPtr data = mCachedRaces.get(id);
        if (data) return data;

        // Create the race data
        data.reset(new RaceData());
        setupRace(id, data);
        mCachedRaces.insert(id, data);
        return data;
    }

    void ActorAdapter::setupActor(const std::string& id, ActorDataPtr data)
    {
        int index = mReferenceables.searchId(id);
        if (index == -1)
        {
            // Record does not exist
            data->reset(id);
            emit actorChanged(id);
            return;
        }

        auto& record = mReferenceables.getRecord(index);
        if (record.isDeleted())
        {
            // Record is deleted and therefore not accessible
            data->reset(id);
            emit actorChanged(id);
            return;
        }

        const int TypeColumn = mReferenceables.findColumnIndex(Columns::ColumnId_RecordType);
        int type = mReferenceables.getData(index, TypeColumn).toInt();
        if (type == UniversalId::Type_Creature)
        {
            // Valid creature record
            setupCreature(id, data);
            emit actorChanged(id);
        }
        else if (type == UniversalId::Type_Npc)
        {
            // Valid npc record
            setupNpc(id, data);
            emit actorChanged(id);
        }
        else
        {
            // Wrong record type
            data->reset(id);
            emit actorChanged(id);
        }
    }

    void ActorAdapter::setupRace(const std::string& id, RaceDataPtr data)
    {
        // Common setup
        data->reset(id);

        int index = mRaces.searchId(id);
        if (index == -1)
        {
            // Record does not exist
            return;
        }

        auto& raceRecord = mRaces.getRecord(index);
        if (raceRecord.isDeleted())
        {
            // Record is deleted, so not accessible
            return;
        }

        // TODO move stuff in actor related to race here

        // Setup body parts
        for (int i = 0; i < mBodyParts.getSize(); ++i)
        {
            std::string partId = mBodyParts.getId(i);
            auto& partRecord = mBodyParts.getRecord(i);

            if (partRecord.isDeleted())
            {
                // Record is deleted, so not accessible.
                continue;
            }

            auto& part = partRecord.get();
            if (part.mRace == id && part.mData.mType == ESM::BodyPart::MT_Skin && !is1stPersonPart(part.mId))
            {
                auto type = (ESM::BodyPart::MeshPart) part.mData.mPart;
                bool female = part.mData.mFlags & ESM::BodyPart::BPF_Female;
                if (female) data->setFemalePart(type, part.mId);
                else data->setMalePart(type, part.mId);
            }
        }
    }

    void ActorAdapter::setupNpc(const std::string& id, ActorDataPtr data)
    {
        // Common setup, record is known to exist and is not deleted
        int index = mReferenceables.searchId(id);
        auto& npc = dynamic_cast<const Record<ESM::NPC>&>(mReferenceables.getRecord(index)).get();

        RaceDataPtr raceData = getRaceData(npc.mRace);
        data->reset(id, !npc.isMale(), raceData);

        // Add inventory items
        for (auto& item : npc.mInventory.mList)
        {
            if (item.mCount <= 0) continue;
            std::string itemId = item.mItem.toString();
            addNpcItem(itemId, data);
        }

        // Add head and hair
        data->setPart(ESM::PRT_Head, npc.mHead);
        data->setPart(ESM::PRT_Hair, npc.mHair);
    }

    void ActorAdapter::addNpcItem(const std::string& itemId, ActorDataPtr data)
    {
        int index = mReferenceables.searchId(itemId);
        if (index == -1)
        {
            // Item does not exist yet
            data->addOtherDependency(itemId);
            return;
        }

        auto& record = mReferenceables.getRecord(index);
        if (record.isDeleted())
        {
            // Item cannot be accessed yet
            data->addOtherDependency(itemId);
            return;
        }

        // Convenience function to add a parts list to actor data
        auto addParts = [&](const ESM::PartReferenceList& list) {
            for (auto& part : list.mParts)
            {
                std::string partId;
                auto partType = (ESM::PartReferenceType) part.mPart;

                if (data->isFemale())
                    partId = part.mFemale;
                if (partId.empty())
                    partId = part.mMale;

                if (!partId.empty()) data->setPart(partType, partId);
            }
        };

        int TypeColumn = mReferenceables.findColumnIndex(Columns::ColumnId_RecordType);
        int type = mReferenceables.getData(index, TypeColumn).toInt();
        if (type == UniversalId::Type_Armor)
        {
            auto& armor = dynamic_cast<const Record<ESM::Armor>&>(record).get();
            addParts(armor.mParts);

            // Changing parts could affect what is picked for rendering
            data->addOtherDependency(itemId);
        }
        else if (type == UniversalId::Type_Clothing)
        {
            auto& clothing = dynamic_cast<const Record<ESM::Clothing>&>(record).get();
            addParts(clothing.mParts);

            // Changing parts could affect what is picked for rendering
            data->addOtherDependency(itemId);
        }
    }

    void ActorAdapter::setupCreature(const std::string& id, ActorDataPtr data)
    {
        data->reset(id);

        // TODO move stuff from Actor here
    }

    void ActorAdapter::markDirtyDependency(const std::string& dep)
    {
        for (auto raceIt : mCachedRaces)
        {
            if (raceIt->hasDependency(dep))
                mDirtyRaces.emplace(raceIt->getId());
        }
        for (auto actorIt : mCachedActors)
        {
            if (actorIt->hasDependency(dep))
                mDirtyActors.emplace(actorIt->getId());
        }
    }

    void ActorAdapter::updateDirty()
    {
        // Handle races before actors, since actors are dependent on race
        for (auto& race : mDirtyRaces)
        {
            RaceDataPtr data = mCachedRaces.get(race);
            if (data)
            {
                setupRace(race, data);
                // Race was changed. Need to mark actor dependencies as dirty.
                // Cannot use markDirtyDependency because that would invalidate
                // the current iterator.
                for (auto actorIt : mCachedActors)
                {
                    if (actorIt->hasDependency(race))
                        mDirtyActors.emplace(actorIt->getId());
                }
            }
        }
        mDirtyRaces.clear();

        for (auto& actor : mDirtyActors)
        {
            ActorDataPtr data = mCachedActors.get(actor);
            if (data)
            {
                setupActor(actor, data);
            }
        }
        mDirtyActors.clear();
    }
}


// Include
#include "instance.h"

#include "objectmanager.h"
#include "lua.h"
#include "serverevent.h"
#include "catalyst.h"
#include "scheduler.h"
#include "mission_progression.h"
#include "mission_results.h"
#include "cashout_rewards.h"
#include "objective_scoring.h"

#include "raknet/server.h"
#include "sporenet/instance.h"
#include "sporenet/user.h"
#include "utils/functions.h"

#include <iostream>
#include <array>
#include <openssl/rand.h>
#include <vector>

/*
	eeh... maybe not

	TODO: Rename Game namespace to SporeNet? Same for RakNet?
		Game -> SporeNet
		RakNet -> SporeNet
*/

// Game
namespace Game {
	// CashOutData
	void CashOutData::WriteTo(RakNet::BitStream& stream) const {
		using RakNet::bytes_to_bits;
		using RakNet::ReallocateStream;

		constexpr auto size = bytes_to_bits(0x2C8);

		auto writeOffset = ReallocateStream(stream, size);

		stream.SetWriteOffset(writeOffset + bytes_to_bits(0x00));
		Write(stream, mPlanetsCompleted);
		Write(stream, mDna);

		stream.SetWriteOffset(writeOffset + bytes_to_bits(0x14));
		for (const auto xp : mStartingExperiences) { Write(stream, xp); }
		for (const auto xp : mFinalExperiences) { Write(stream, xp); }

		stream.SetWriteOffset(writeOffset + bytes_to_bits(0x34));
		for (const auto medalCount : mGoldMedals) { Write(stream, medalCount); }
		for (const auto medalCount : mSilverMedals) { Write(stream, medalCount); }
		for (const auto medalCount : mBronzeMedals) { Write(stream, medalCount); }

		// Recovered 103 client boundaries are the upper Rarified/Purified
		// percentage and the lower Purified percentage, respectively.
		stream.SetWriteOffset(writeOffset + bytes_to_bits(0x64));
		for (const auto value : mUpperRarityBoundaries) { Write(stream, value); }
		for (const auto value : mLowerRarityBoundaries) { Write(stream, value); }

		stream.SetWriteOffset(writeOffset + bytes_to_bits(0x84));
		for (const auto value : mDailyBonuses) { Write(stream, value); }
		for (std::size_t player = 0; player < mRewards.size(); ++player) {
			for (std::size_t item = 0; item < mRewards[player].size(); ++item) {
				const auto& reward = mRewards[player][item];
				constexpr std::size_t rowSize = 0x90;
				constexpr std::size_t itemSize = 0x24;
				const auto offset = 0x88 + rowSize * player + itemSize * item + 0x08;
				stream.SetWriteOffset(writeOffset + bytes_to_bits(offset));
				Write(stream, reward.rigblock);
				Write(stream, reward.suffix);
				Write(stream, reward.prefix);
				Write(stream, reward.secondaryPrefix);
				Write(stream, reward.level);
				Write(stream, reward.rarity);
				Write(stream, reward.roll);
			}
		}
		stream.SetWriteOffset(writeOffset + size);
	}

	// Instance
	InstancePtr Instance::Create(uint32_t id) {
		return InstancePtr(new Instance(id));
	}

	Instance::Instance(uint32_t id) {
		mData.id = id;

		constexpr std::array<std::string_view, 5> objectiveTypes {
			"FinishLevelQuickly",
			"DoDamageOften",
			"TouchAllObelisks",
			"DefeatAllMonsters",
			"HugeDamage"
		};

		for (const auto& objectiveName : objectiveTypes) {
			decltype(auto) objective = mObjectives.emplace_back();
			objective.id = utils::hash_id(objectiveName);
			objective.value = 0;
			objective.medal = RakNet::ObjectiveMedal::InProgress;
			objective.description = std::string(objectiveName);
		}
	}

	Instance::~Instance() {
		Stop();
	}

	bool Instance::Start() {
		Stop();
		if (RAND_bytes(reinterpret_cast<unsigned char*>(&mRunNonce),
			static_cast<int>(sizeof(mRunNonce))) != 1 || mRunNonce == 0) {
			return false;
		}
		// A restarted instance must rebuild its level and object graph.
		mGameStarted = false;
		mLevelLoaded = false;
		mMarkers.clear();
		mPlayerSpawnpoints.clear();
		mEnemyClearTracker.Reset();
		mDamageObjectives.Reset();
		mTouchedObelisks.clear();
		mScoredPlayers.clear();
		for (auto& objective : mObjectives) {
			objective.value = 0;
			objective.medal = RakNet::ObjectiveMedal::InProgress;
		}
		mCashOutData = CashOutData{};

		mObjectManager = std::make_unique<ObjectManager>(*this);
		mLua = std::make_unique<Lua>(*this);
		mServer = std::make_unique<RakNet::Server>(*this);

		mServer->start(mData.hostNetwork.exip.port);
		mGameStartTime = utils::get_milliseconds();
		mGameTime = mGameStartTime;
		mGameTimeLua = mGameStartTime;

		mServer->add_task([this] {
			mLua->Initialize();
		});
		return true;
	}

	void Instance::Stop() {
		if (!mEvents.empty()) {
			auto& scheduler = GetApp().GetScheduler();
			for (uint32_t id : mEvents) {
				scheduler.CancelTask(id);
			}
		}

		mEvents.clear();
		if (mServer && mServer->is_running()) {
			mServer->stop();
			// wait until it stops
		}

		mServer.reset();
		mLua.reset();
		mObjectManager.reset();
	}

	bool Instance::LoadLevel() {
		if (mLevelLoaded) {
			return true;
		}

		std::string levelName = mChainData.GetName().data();
		mLevelLoaded = mLevel.Load(mChainData.GetDifficultyName(), levelName);
		if (mLevelLoaded) {
			LevelConfig config;
			if (mChainData.IsCompleted()) {
				config = mLevel.GetConfig();
			} else {
				config = mLevel.GetFirstTimeConfig();
			}

			const auto& nounDatabase = NounDatabase::Instance();
			for (uint32_t i = 0; i < 3; ++i) {
				const auto& minion = config.GetMinion(i);
				const auto& minionNounName = minion.GetNounName();
				mChainData.SetEnemyNoun(minionNounName, i);

				const auto& special = config.GetSpecial(i);
				const auto& specialNounName = special.GetNounName();
				mChainData.SetEnemyNoun(specialNounName, i + 3);

				if (!minionNounName.empty()) {
					auto noun = nounDatabase.Get(utils::hash_id(minionNounName));
					if (noun && noun->GetNonPlayerClassData()) {
						std::cout << noun->GetNonPlayerClassData()->GetName() << std::endl;
					}
				}

				if (!specialNounName.empty()) {
					auto noun = nounDatabase.Get(utils::hash_id(special.GetNounName()));
					if (noun && noun->GetNonPlayerClassData()) {
						std::cout << noun->GetNonPlayerClassData()->GetName() << std::endl;
					}
				}
			}

			Markerset markerset;
			if (mLevel.GetMarkerset(levelName + "_design.Markerset", markerset)) {
				const auto& markers = markerset.GetMarkersByType(utils::hash_id("CameraSpawnPoint.Noun"));
				for (const auto& marker : markers) {
					mPlayerSpawnpoints.push_back(marker->GetPosition());
				}
			}

			// Check design_spawners
			if (mPlayerSpawnpoints.empty()) {
				if (mLevel.GetMarkerset(levelName + "_design_spawners.Markerset", markerset)) {
					const auto& markers = markerset.GetMarkersByType(utils::hash_id("CameraSpawnPoint.Noun"));
					for (const auto& marker : markers) {
						mPlayerSpawnpoints.push_back(marker->GetPosition());
					}
				}
			}
		}

		return mLevelLoaded;
	}

	bool Instance::PrepareNextPlanet(uint8_t requestingPlayer, bool startNewChain) {
		// The complete planet is still cash-out eligible. The client may
		// explicitly request another deployment instead of cashing out.
		// Campaign planet order follows the bundled levelNames table until
		// the real chain vote-selection protocol is reconstructed.
		if (!mServer || !mChainData.IsCompleted() ||
			(!startNewChain && !Progression::CanDeployNextPlanet(
				mChainData.GetProgression(), mChainData.GetLevelIndex()))) {
			return false;
		}
		uint64_t newNonce = mRunNonce;
		if (startNewChain &&
			(RAND_bytes(reinterpret_cast<unsigned char*>(&newNonce),
				static_cast<int>(sizeof(newNonce))) != 1 || newNonce == 0)) {
			return false;
		}
		for (const auto& [_, client] : mServer->GetClients()) {
			if (!client || client->GetId() == requestingPlayer) {
				continue;
			}
			// Never discard another player's live dungeon or pending vote.
			// Coordinated multiplayer chain voting requires a client trace.
			const auto state = client->GetGameState();
			if (state != GameState::Spaceship && state != GameState::ChainCashOut) {
				return false;
			}
		}

		// Cancel callbacks from the old dungeon before its Lua VM and
		// ObjectManager become invalid.
		for (uint32_t taskId : mEvents) {
			GetApp().GetScheduler().CancelTask(taskId);
		}
		mEvents.clear();
		for (const auto& [_, player] : mPlayers) {
			if (player) {
				player->ClearDungeonObjects();
			}
		}
		mLua.reset();
		mObjects.clear();
		mObjectManager.reset();

		mMarkers.clear();
		mPlayerSpawnpoints.clear();
		mEnemyClearTracker.Reset();
		mDamageObjectives.Reset();
		mTouchedObelisks.clear();
		mScoredPlayers.clear();
		for (auto& objective : mObjectives) {
			objective.value = 0;
			objective.medal = RakNet::ObjectiveMedal::InProgress;
		}
		mGameStarted = false;
		mLevelLoaded = false;
		if (startNewChain) {
			// Cash-out followed by a fresh lobby deployment begins a new
			// run, not another planet of the previous completed chain.
			mRunNonce = newNonce;
			mChainData = RakNet::ChainVoteData{};
			mCashOutData = CashOutData{};
		} else {
			mChainData.SetCompleted(false);
			mChainData.SetLevelByIndex(mChainData.GetLevelIndex() + 1);
		}
		mObjectManager = std::make_unique<ObjectManager>(*this);
		mLua = std::make_unique<Lua>(*this);
		mLua->Initialize();

		mGameStartTime = utils::get_milliseconds();
		mGameTime = mGameStartTime;
		mGameTimeLua = mGameStartTime;
		// The current chain retains its accumulated medals; a new chain
		// starts with an empty result payload.
		return true;
	}

	MarkerPtr Instance::GetMarker(uint32_t id) const {
		auto it = mMarkers.find(id);
		return (it != mMarkers.end()) ? it->second : nullptr;
	}

	PlayerPtr Instance::GetPlayer(int64_t id) const {
		auto it = mPlayers.find(id);
		return (it != mPlayers.end()) ? it->second : nullptr;
	}

	PlayerPtr Instance::GetPlayerByIndex(uint8_t index) const {
		for (const auto& [_, player] : mPlayers) {
			if (player->GetId() == index) {
				return player;
			}
		}
		return nullptr;
	}

	PlayerPtr Instance::AddPlayer(const SporeNet::UserPtr& user, uint8_t index) {
		if (!user) {
			return nullptr;
		}

		auto it = mPlayers.try_emplace(user->get_id());
		if (it.second) {
			it.first->second = std::make_shared<Player>(*this, user, index);
		}

		return it.first->second;
	}

	void Instance::RemovePlayer(int64_t id) {
		mPlayers.erase(id);
	}

	uint32_t Instance::GetId() const {
		return mData.id;
	}

	uint64_t Instance::GetTime() const {
		return mGameTime;
	}

	uint64_t Instance::GetTimeElapsed() const {
		return utils::get_milliseconds() - mGameStartTime;
	}

	ObjectManager& Instance::GetObjectManager() {
		return *mObjectManager;
	}

	const ObjectManager& Instance::GetObjectManager() const {
		return *mObjectManager;
	}

	Lua& Instance::GetLua() {
		return *mLua;
	}

	const Lua& Instance::GetLua() const {
		return *mLua;
	}
	
	void Instance::AddServerTask(std::function<void(void)> task) {
		mServer->add_task(std::move(task));
	}

	void Instance::AddClientTask(uint8_t id, RakNet::PacketID packet) {
		mServer->add_client_task(id, packet);
	}

	void Instance::OnObjectCreate(const ObjectPtr& object) {
		//
	}

	void Instance::OnObjectDestroy(const ObjectPtr& object) {
		//
	}

	std::vector<RakNet::Objective> Instance::GetObjectivesForPlayer(uint8_t playerId, bool completed) const {
		// A mission has global objectives and player-specific objectives.
		// Construct a snapshot for each client; never mutate the shared
		// objective array when serializing another player's result.
		auto objectives = mObjectives;
		const uint32_t major = mChainData.GetLevelIndex() == 0 ? 0U :
			std::min<uint32_t>(mChainData.GetMajorDifficulty(), 7U);
		for (auto& objective : objectives) {
			if (objective.id == utils::hash_id("FinishLevelQuickly")) {
				if (completed) {
					const auto seconds = GetTimeElapsed() / 1000;
					objective.value = static_cast<uint32_t>(
						std::min<uint64_t>(seconds, UINT32_MAX));
					objective.medal = static_cast<RakNet::ObjectiveMedal>(
						ObjectiveScoring::ScoreFinishLevelQuickly(seconds));
				}
			} else if (objective.id == utils::hash_id("DefeatAllMonsters")) {
				objective.value = mEnemyClearTracker.GetKillPercent();
				if (completed) {
					objective.medal = static_cast<RakNet::ObjectiveMedal>(
						mEnemyClearTracker.GetMedal());
				}
			} else if (objective.id == utils::hash_id("TouchAllObelisks")) {
				objective.value = static_cast<uint32_t>(
					std::min<std::size_t>(mTouchedObelisks.size(), UINT32_MAX));
				if (completed) {
					objective.medal = static_cast<RakNet::ObjectiveMedal>(
						ObjectiveScoring::ScoreTouchedObelisks(mTouchedObelisks.size()));
				}
			} else if (objective.id == utils::hash_id("DoDamageOften")) {
				objective.value = mDamageObjectives.GetHitCount(playerId);
				if (completed) {
					objective.medal = static_cast<RakNet::ObjectiveMedal>(
						mDamageObjectives.ScoreDamageOften(playerId, major));
				}
			} else if (objective.id == utils::hash_id("HugeDamage")) {
				objective.value = mDamageObjectives.GetHugeHitCount(playerId);
				if (completed) {
					objective.medal = static_cast<RakNet::ObjectiveMedal>(
						mDamageObjectives.ScoreHugeDamage(playerId));
				}
			}
		}
		return objectives;
	}

	void Instance::OnDamageDealt(const ObjectPtr& attacker, const ObjectPtr& victim, float appliedDamage) {
		if (!attacker || !victim || !mGameStarted || !mServer ||
			!attacker->IsPlayerControlled() || attacker->GetTeam() == victim->GetTeam()) {
			return;
		}
		const auto& noun = victim->GetNoun();
		if (!noun || !noun->GetNonPlayerClassData() || victim->IsPlayerControlled()) {
			return;
		}
		const auto& player = attacker->GetPlayer();
		if (!player) {
			return;
		}
		const auto playerId = player->GetId();
		// Health scaling for HugeDamage is still unimplemented in the server.
		// A base scale of 1 is explicit, rather than fabricating a
		// difficulty multiplier. Count only damage that actually removed HP.
		if (!mDamageObjectives.RecordHit(playerId, appliedDamage, 1.0f)) {
			return;
		}
		const auto& client = mServer->GetClient(playerId);
		if (!client) {
			return;
		}
		for (std::size_t i = 0; i < mObjectives.size(); ++i) {
			const auto& objective = mObjectives[i];
			if (objective.id == utils::hash_id("DoDamageOften") ||
				objective.id == utils::hash_id("HugeDamage")) {
				// The serializer reads this player's counters, not the
				// last player's shared objective value.
				mServer->SendObjectiveUpdate(client, static_cast<uint8_t>(i), 0);
			}
		}
	}

	void Instance::OnObjectDeath(const ObjectPtr& object, bool critical, bool knockback) {
		if (!object) {
			return;
		}
		object->OnDeath();

		// Count only enemies whose world-spawn was already registered.
		// Corpses and repeated kill notifications must never raise the score.
		if (mEnemyClearTracker.RecordDeath(object->GetId())) {
			for (std::size_t i = 0; i < mObjectives.size(); ++i) {
				auto& objective = mObjectives[i];
				if (objective.id == utils::hash_id("DefeatAllMonsters")) {
					const uint32_t percent = mEnemyClearTracker.GetKillPercent();
					if (objective.value != percent) {
						objective.value = percent;
						if (mServer) {
							for (const auto& [_, client] : mServer->GetClients()) {
								if (client) {
									mServer->SendObjectiveUpdate(client, static_cast<uint8_t>(i), 0);
								}
							}
						}
					}
					break;
				}
			}
		}

		SendObjectGfxState(object, utils::hash_id("dead"));

		const auto& noun = object->GetNoun();
		if (noun) {
			if (noun->IsCreature()) {
				object->SetHasCollision(false);
			}

			const auto& animation = noun->GetCharacterAnimation();
			if (animation) {
				uint32_t deathState = 0;
				if (critical) {
					// replace 0 with damage type
					uint8_t type = 0 * static_cast<uint8_t>(critical);
					type += static_cast<uint8_t>(knockback);

					std::tie(deathState, std::ignore) = animation->GetAnimationData(
						utils::enum_helper<CharacterAnimationType>::next(CharacterAnimationType::MeleeCriticalDeath, type)
					);
				} else {
					std::tie(deathState, std::ignore) = animation->GetAnimationData(CharacterAnimationType::MeleeDeath);
				}

				if (deathState == 0) {
					std::tie(deathState, std::ignore) = animation->GetAnimationData(CharacterAnimationType::Death);
				}

				if (deathState != 0) {
					SendAnimationState(object, deathState);
				}
			}

			if (critical) {
				object->MarkForDeletion();
			}
		}
	}

	void Instance::OnPlayerStart(const PlayerPtr& player) {
		if (!player || !mLua || !mServer || !mObjectManager) {
			return;
		}
		std::cout << "Player Start" << std::endl;

		const bool initializeWorld = !mGameStarted;
		if (initializeWorld) {
			mLua->PreloadAbilities();
			mGameStarted = LoadLevel();
		}
		if (!mGameStarted) {
			return;
		}
		if (initializeWorld) {
			// Score the mission from actual dungeon entry, never from the
			// earlier server startup or time spent waiting in the lobby.
			mGameStartTime = utils::get_milliseconds();
			mGameTime = mGameStartTime;
			mGameTimeLua = mGameStartTime;
		}

		// Shared enemies, obelisks and markers must be spawned once per
		// instance, not again for every player entering the dungeon.
		if (initializeWorld) {
			std::string levelName = mChainData.GetName().data();

			// create level objects and whatnot
			Markerset markerset;
			if (mLevel.GetMarkerset(levelName + "_obelisk_1.Markerset", markerset)) {
				const auto& healthObelisks = markerset.GetMarkersByType(utils::hash_id("prefab_health_obelisk.Noun"));
				for (const auto& marker : healthObelisks) {
					mObjectManager->Create(marker);
				}

				const auto& bossObelisks = markerset.GetMarkersByType(utils::hash_id("prefab_boss_obelisk.Noun"));
				for (const auto& marker : bossObelisks) {
					mObjectManager->Create(marker);
				}
			}

			constexpr std::array<const char*, 2> designSets { "_design", "_design_spawners" };
			for (const char* designSet : designSets) {
				if (mLevel.GetMarkerset(levelName + designSet + ".Markerset", markerset)) {
					for (const auto& marker : markerset.GetMarkers()) {
						mMarkers[marker->GetId()] = marker;

						const auto& teleporterData = marker->GetTeleporterData();
						if (teleporterData) {
							mObjectManager->Create(marker);
						}
					}
				}
			}

			const auto TestEnemy = [this](const auto& markerset) {
				for (const auto& marker : markerset.GetMarkers()) {
					mMarkers[marker->GetId()] = marker;

					switch (marker->GetNoun()) {
						case utils::hash_id("SpawnPoint_Director.Noun"):
						case utils::hash_id("SpawnPoint_DirectorA.Noun"):
						case utils::hash_id("SpawnPoint_DirectorB.Noun"):
						case utils::hash_id("SpawnPoint_DirectorWanderer.Noun"):
						{
							const auto& object = mObjectManager->Create(mChainData.GetEnemyNoun(utils::random::get<uint32_t>(0, 5)));
							if (object) {
								object->SetPosition(marker->GetPosition());
								object->SetOrientation(marker->GetRotation());
							}
							break;
						}

						default:
							// Other marker types are not enemy spawners.
							break;
					}
				}
			};

			if (mLevel.GetMarkerset(levelName + "_AI_Wander.Markerset", markerset)) {
				TestEnemy(markerset);
			}
			
			if (mLevel.GetMarkerset(levelName + "_AI_WandererA.Markerset", markerset) || mLevel.GetMarkerset(levelName + "_AI_Wanderers_A.Markerset", markerset)) {
				TestEnemy(markerset);
			}

			if (mLevel.GetMarkerset(levelName + "_AI_WandererB.Markerset", markerset) || mLevel.GetMarkerset(levelName + "_AI_Wanderers_B.Markerset", markerset)) {
				TestEnemy(markerset);
			}

			if (mLevel.GetMarkerset(levelName + "_AI_WandererC.Markerset", markerset) || mLevel.GetMarkerset(levelName + "_AI_Wanderers_C.Markerset", markerset)) {
				TestEnemy(markerset);
			}
			
		}

		// mLua->LoadFile("data/serverdata/lua/player_start.lua");

		// Set objectives
		const auto& client = mServer->GetClient(player->GetId());
		if (client) {
			mServer->SendObjectivesInitForLevel(client);
			for (uint8_t i = 0, l = static_cast<uint8_t>(mObjectives.size()); i < l; ++i) {
				mServer->SendObjectiveUpdate(client, i, 0);
			}

			// Snapshot existing objects for the entering client. The world
			// object flag is global, so it cannot indicate which clients have
			// received a create packet.
			for (const auto& object : mObjectManager->GetActiveObjects()) {
				if (!object) {
					continue;
				}
				if (initializeWorld) {
					SendObjectCreate(object);
				} else {
					mServer->SendObjectCreate(client, object);
					if (const auto& data = object->GetCombatantData()) {
						mServer->SendCombatantDataUpdate(client, object, *data);
					}
					if (const auto& data = object->GetAttributeData()) {
						mServer->SendAttributeDataUpdate(client, object, *data);
					}
					if (const auto& data = object->GetLootData()) {
						mServer->SendLootDataUpdate(client, object, *data);
					}
					if (const auto& data = object->GetInteractableData()) {
						mServer->SendInteractableDataUpdate(client, object, *data);
					}
					if (const auto& data = object->GetAgentBlackboardData()) {
						mServer->SendAgentBlackboardUpdate(client, object, *data);
					}
				}
			}
		}

		// Create creatures
		glm::vec3 spawnpoint;
		if (player->GetId() < mPlayerSpawnpoints.size()) {
			spawnpoint = mPlayerSpawnpoints[player->GetId()];
		}

		// Force send character objects so the client can find them when swapping.
		for (uint32_t i = 0; i < 3; ++i) {
			const auto& characterObject = player->GetCharacterObject(i);
			if (!characterObject) {
				continue;
			}

			characterObject->SetPosition(spawnpoint);
			characterObject->SetVisible(true); // i == creatureIndex

			characterObject->SetAttributeValue(AttributeType::InvisibleToSecurityTeleporters, 1);
			characterObject->SetAttributeValue(AttributeType::AttackSpeedScale, 1);
			characterObject->SetAttributeValue(AttributeType::CooldownScale, 1);

			SendObjectUpdate(characterObject);
		}
	}

	uint32_t Instance::AddTask(uint32_t delay, std::function<void(uint32_t)> task) {
		auto id = GetApp().GetScheduler().AddTask(delay, task);
		mEvents.insert(id);
		return id;
	}

	void Instance::CancelTask(uint32_t id) {
		GetApp().GetScheduler().CancelTask(id);
		mEvents.erase(id);
	}

	bool Instance::ServerUpdate() const {
		return (utils::get_milliseconds() - mGameTime) >= 10;
	}

	bool Instance::Update() {
		// It is safe to send packets in this function.
		auto newGameTime = utils::get_milliseconds();

		if (auto delta = (newGameTime - mGameTimeLua); delta >= 1) {
			mGameTimeLua = newGameTime;
			mLua->Update();
		}

		if (auto delta = (newGameTime - mGameTime); delta >= 50) {
			mGameTime = newGameTime;
			mObjectManager->Update(delta / 1000.f); // Maybe send update time so we can compensate for lag?
			for (const auto& [_, player] : mPlayers) {
				SendLabsPlayerUpdate(player);
			}

			if (!mObjectives.empty()) {
				auto& objective = mObjectives.front();
				if (objective.id == utils::hash_id("FinishLevelQuickly")) {
					const auto seconds = static_cast<uint32_t>(
						std::min<uint64_t>(GetTimeElapsed() / 1000, UINT32_MAX));
					if (objective.value != seconds) {
						objective.value = seconds;
						// Timed objectives change once per second. Do not issue the
						// unrelated obelisk voiceover every 50 ms or omit other players.
						for (const auto& [_, client] : mServer->GetClients()) {
							if (client) {
								mServer->SendObjectiveUpdate(client, 0, 0);
							}
						}
					}
				}
			}

			return true;
		}

		return false;
	}

	void Instance::MoveObject(const ObjectPtr& object, const Locomotion& locomotionData) {
		if (!object) {
			return;
		}

		const auto goalFlags = locomotionData.GetGoalFlags();
		if (goalFlags & 0x002) {
			object->SetOrientation(glm::quat(locomotionData.GetFacing()));
		}

		bool teleport = goalFlags & 0x020;
		if (teleport) {
			object->SetPosition(locomotionData.GetGoalPosition());
			for (const auto& [_, client] : mServer->GetClients()) {
				mServer->SendObjectTeleport(client, object, object->GetPosition(), object->GetOrientation());
			}
		} else {
			object->SetPosition(locomotionData.GetPartialGoalPosition());
			for (const auto& [_, client] : mServer->GetClients()) {
				mServer->SendObjectPlayerMove(client, object, locomotionData);
			}
		}
	}

	bool Instance::UseAbility(const ObjectPtr& object, const RakNet::CombatData& combatData) {
		if (!object || combatData.abilityId == 0 || object->HasCooldown(combatData.abilityId)) {
			return false;
		}

		const auto& ability = mLua->GetAbility(combatData.abilityId);
		if (!ability) {
			return false;
		}

		// Tick returns false when the script is unavailable or the actor's
		// existing Lua coroutine cannot accept another action.
		return ability->Tick(object, mObjectManager->Get(combatData.targetId),
			combatData.cursorPosition, combatData.abilityRank);
	}

	void Instance::SwapCharacter(const PlayerPtr& player, uint32_t creatureIndex) {
		if (!player) {
			return;
		}

		// TODO: proper functions for these things
		if (!player->SwapCharacter(creatureIndex)) {
			return;
		}

		// set player current creature
		for (const auto& [_, client] : mServer->GetClients()) {
			mServer->SendPlayerCharacterDeploy(client, player, creatureIndex);
		}

		SendLabsPlayerUpdate(player);
	}

	void Instance::InteractWithObject(const PlayerPtr& player, uint32_t objectId) {
		if (!player || !mObjectManager) {
			return;
		}
		const auto& object = mObjectManager->Get(objectId);
		if (!object || object->IsMarkedForDeletion()) {
			return;
		}
		const auto& noun = object->GetNoun();
		if (!noun) {
			return;
		}

		const auto& interactableData = object->GetInteractableData();
		// Loot pickups are single-use. Never consume a use on a failed pickup
		// (full catalyst grid, missing loot data, or failed inventory save).
		if (interactableData &&
			(interactableData->GetUsesAllowed() <= 0 ||
			 interactableData->GetTimesUsed() >= interactableData->GetUsesAllowed())) {
			return;
		}
		// The original objective_Obelisks.lua handles distinct
		// InteractWithObelisk actions, counting each object only once.
		if (interactableData &&
			interactableData->GetAbility() == utils::hash_id("InteractWithObelisk")) {
			if (!mTouchedObelisks.insert(objectId).second) {
				return;
			}
			interactableData->SetTimesUsed(interactableData->GetTimesUsed() + 1);
			if (mServer) {
				for (const auto& [_, client] : mServer->GetClients()) {
					if (client) {
						mServer->SendInteractableDataUpdate(client, object, *interactableData);
					}
				}
			}
			for (std::size_t i = 0; i < mObjectives.size(); ++i) {
				auto& objective = mObjectives[i];
				if (objective.id == utils::hash_id("TouchAllObelisks")) {
					objective.value = static_cast<uint32_t>(
						std::min<std::size_t>(mTouchedObelisks.size(), UINT32_MAX));
					if (mServer) {
						for (const auto& [_, client] : mServer->GetClients()) {
							if (client) {
								mServer->SendObjectiveUpdate(client, static_cast<uint8_t>(i),
									utils::hash_id("vo_ship_obelisk_accessed"));
							}
						}
					}
					break;
				}
			}
			return;
		}

		switch (noun->GetType()) {
			case NounType::Loot:
				PickupLoot(player, object);
				break;
			case NounType::Crystal:
				PickupCatalyst(player, object);
				break;
			default:
				// Other objects need their own scripted interaction handlers.
				// The old fallback generated unlimited catalysts and health orbs.
				return;
		}
		if (object->IsMarkedForDeletion() && interactableData) {
			interactableData->SetTimesUsed(interactableData->GetTimesUsed() + 1);
		}
	}

	void Instance::CancelAction(const PlayerPtr& player, const ObjectPtr& object) {
		// TODO: Change this to not force lua updates
		if (!player || !object) {
			return;
		}

		auto luaThread = object->GetLuaThread();
		if (!luaThread) {
			// No active action
			return;
		}

		luaThread->stop();
		/*
		sol::table privateTable = mLua->GetPrivateTable(object->GetId());
		if (privateTable == sol::nil) {
			return;
		}

		if (sol::object action = privateTable["CURRENT_ACTION"]; action.is<uint32_t>()) {
			CancelTask(action.as<uint32_t>());
			privateTable["CURRENT_ACTION"] = sol::nil;
		}
		*/
	}

	bool Instance::PickupDNA(const ObjectPtr& loot, const ObjectPtr& character) {
		if (!mObjectManager || !mServer || !loot || !character ||
			loot->IsMarkedForDeletion() || character->IsMarkedForDeletion() ||
			loot->GetNounId() != utils::hash_id("DNA.noun") ||
			mObjectManager->Get(loot->GetId()) != loot ||
			mObjectManager->Get(character->GetId()) != character ||
			!character->IsPlayerControlled() || character->GetHealth() <= 0.f) {
			return false;
		}
		const auto& player = character->GetPlayer();
		const auto& lootData = loot->GetLootData();
		if (!player || player->GetDeployedCharacterObject() != character ||
			!lootData || lootData->GetPart() ||
			!MissionResults::DecodeDnaPickup(lootData->GetDNAAmount())) {
			return false;
		}

		// PickupLoot performs one durable save, rolls back on failure, and
		// marks the world object for deletion only after success.
		PickupLoot(player, loot);
		return loot->IsMarkedForDeletion();
	}

	void Instance::DropLoot(const PlayerPtr& player, uint64_t lootInstanceId) {
		if (!player) {
			return;
		}

		const auto& user = player->GetUser();
		if (!user) {
			return;
		}

		// get temporary part and drop
	}

	void Instance::DropLoot() {
		constexpr std::array<std::string_view, 4> dropRarityNouns {
			"loot_container_white.Noun",
			"loot_container_green.Noun",
			"loot_drop.noun",
			"loot_container_purple.Noun"
		};

		const auto& clients = mServer->GetClients();
		if (clients.empty()) {
			return;
		}

		const auto& firstClient = clients.begin()->second;
		if (!firstClient) {
			return;
		}
		const auto& player = firstClient->GetPlayer();
		if (!player) {
			return;
		}
		const auto characterObject = player->GetDeployedCharacterObject();
		if (!characterObject) {
			return;
		}
		auto position = characterObject->GetPosition();

		static uint64_t lootId = 0;
		for (uint32_t i = 0; i < 4; ++i) {
			auto part = SporeNet::Part(i);
			part.SetRarity(static_cast<SporeNet::PartRarity>(i));

			//
			auto containerType = static_cast<int32_t>(part.GetRarity());
			if (containerType >= dropRarityNouns.size()) {
				containerType = 0;
			}

			auto object = mObjectManager->Create(utils::hash_id(dropRarityNouns[containerType]));
			if (!object) {
				continue;
			}
			object->SetPosition(position);
			object->SetInteractableState(0);
			
			const auto& lootData = object->CreateLootData();
			lootData->SetId(++lootId);
			lootData->SetInstanceId(lootId);
			lootData->SetPart(part);

			const auto& interactableData = object->CreateInteractableData();
			interactableData->SetUsesAllowed(1);
			interactableData->SetTimesUsed(0);
			interactableData->SetAbility(utils::hash_id("PickUpLoot"));

			if (SendObjectCreate(object)) {
				for (const auto& [_, client] : clients) {
					mServer->SendLootDataUpdate(client, object, *lootData);
					mServer->SendInteractableDataUpdate(client, object, *interactableData);
				}
			}

			mObjects.push_back(std::move(object));
			position.x += 5;
		}
	}

	void Instance::DropLoot(const ObjectPtr& object, const ObjectPtr& targetObject, DropType dropType) {
		if (!object || !targetObject) {
			return;
		}

		const auto& interactableData = object->GetInteractableData();
		if (interactableData) {

		}

		const auto& position = object->GetPosition(); // client uses "GetClosestPosition" but no way in hell im implementing that.
		const auto& footprintRadius = object->GetFootprintRadius();
		if (utils::enum_helper<DropType>::test(dropType, DropType::Orbs)) {
			float orbDifficultyScale;
			if (mChainData.GetLevelIndex() < 72) {
				orbDifficultyScale = 5.f;
			} else {
				orbDifficultyScale = 1.f;
			}

			uint32_t orbType = 1;
			uint32_t orbServerEvent = 0;
			switch (orbType) {
				case 1:
					orbType = utils::hash_id("HealthOrb.Noun");
					orbServerEvent = utils::hash_id("health_orb_drop.ServerEventDef");
					break;

				case 2:
					orbType = utils::hash_id("ManaOrb.Noun");
					orbServerEvent = utils::hash_id("mana_orb_drop.ServerEventDef");
					break;

				case 3:
					orbType = utils::hash_id("ResurrectOrb.Noun");
					orbServerEvent = utils::hash_id("resurrect_orb_drop.ServerEventDef");
					break;
			}

			if (orbType) {
				ServerEvent serverEvent;
				serverEvent.mServerEventDef = orbServerEvent;
				serverEvent.mTargetPoint = position;

				const auto& object = mObjectManager->Create(orbType);
				if (object) {
					object->SetPosition(position);
					if (SendObjectCreate(object)) {
						SendServerEvent(serverEvent);
					}
				}
			}
		}

		if (utils::enum_helper<DropType>::test(dropType, DropType::Catalysts)) {
			auto crystalFind = targetObject->GetAttributeValue(AttributeType::CrystalFind);
			switch (object->GetTeam()) {
				case 2:
					break;

				default:
					break;
			}

			static uint64_t test_id = 0;

			Catalyst catalyst(CatalystType::AttackSpeed, CatalystRarity::Common, false);

			const auto& object = mObjectManager->Create(catalyst.GetNounId());
			if (!object) {
				return;
			}
			object->SetPosition(position);

			const auto& lootData = object->CreateLootData();
			lootData->SetId(++test_id);
			lootData->SetCatalyst(catalyst);

			const auto& interactableData = object->CreateInteractableData();
			interactableData->SetUsesAllowed(1);
			interactableData->SetTimesUsed(0);
			interactableData->SetAbility(utils::hash_id("PickUpLoot"));

			SendObjectCreate(object);
		}

		if (utils::enum_helper<DropType>::test(dropType, DropType::DNA)) {
			//
		}

		if (utils::enum_helper<DropType>::test(dropType, DropType::Loot)) {
			//
		}
	}

	bool Instance::DropCatalyst(const PlayerPtr& player, uint32_t catalystSlot) {
		if (!player || !mObjectManager || !mServer || !player->IsValidCatalystSlot(catalystSlot)) {
			return false;
		}
		const auto characterObject = player->GetDeployedCharacterObject();
		const auto catalyst = player->GetCatalyst(catalystSlot);
		if (!characterObject || !catalyst) {
			return false;
		}

		static uint64_t lootId = 0;
		const auto object = mObjectManager->Create(catalyst.GetNounId());
		if (!object) {
			return false;
		}
		object->SetPosition(characterObject->GetPosition());
		object->SetInteractableState(0);

		const auto& lootData = object->CreateLootData();
		lootData->SetId(++lootId);
		lootData->SetCatalyst(catalyst);

		const auto& interactableData = object->CreateInteractableData();
		interactableData->SetUsesAllowed(1);
		interactableData->SetTimesUsed(0);
		interactableData->SetAbility(utils::hash_id("PickUpLoot"));

		if (!SendObjectCreate(object)) {
			object->MarkForDeletion();
			return false;
		}
		for (const auto& [_, client] : mServer->GetClients()) {
			mServer->SendLootDataUpdate(client, object, *lootData);
			mServer->SendInteractableDataUpdate(client, object, *interactableData);
		}
		// Do not clear the inventory slot until its world item exists.
		player->SetCatalyst(Catalyst(), catalystSlot);
		return true;
	}

	void Instance::DropCatalyst() {
		const auto& clients = mServer->GetClients();
		if (clients.empty()) {
			return;
		}

		const auto& firstClient = clients.begin()->second;
		if (!firstClient) {
			return;
		}
		const auto& player = firstClient->GetPlayer();
		if (!player) {
			return;
		}
		auto characterObject = player->GetDeployedCharacterObject();
		if (!characterObject) {
			return;
		}

		auto position = characterObject->GetPosition();

		static uint64_t lootId = 0;
		for (uint32_t i = 0; i < 4; ++i) {
			Catalyst catalyst(static_cast<CatalystType>(i), static_cast<CatalystRarity>(i % 3), i & 1);

			auto object = mObjectManager->Create(catalyst.GetNounId());
			if (!object) {
				continue;
			}
			object->SetPosition(position);
			object->SetInteractableState(0);

			const auto& lootData = object->CreateLootData();
			lootData->SetId(++lootId);
			lootData->SetCatalyst(catalyst);

			const auto& interactableData = object->CreateInteractableData();
			interactableData->SetUsesAllowed(1);
			interactableData->SetTimesUsed(0);
			interactableData->SetAbility(utils::hash_id("PickUpLoot"));

			if (SendObjectCreate(object)) {
				for (const auto& [_, client] : clients) {
					mServer->SendLootDataUpdate(client, object, *lootData);
					mServer->SendInteractableDataUpdate(client, object, *interactableData);
				}
			}

			mObjects.push_back(std::move(object));
			position.x += 5;
		}
	}

	bool Instance::CommitCashOut(const PlayerPtr& player) {
		if (!player || !mServer || !mChainData.IsCompleted() ||
			!mScoredPlayers.contains(player->GetId()) || mRunNonce == 0) {
			return false;
		}
		const auto slot = player->GetId();
		if (slot >= mCashOutData.mRewards.size()) {
			return false;
		}
		const auto& user = player->GetUser();
		if (!user || user->get_id() <= 0) {
			return false;
		}
		auto& account = user->get_account();
		const uint64_t receiptId = MissionResults::MakeCashOutReceiptId(
			mRunNonce, static_cast<uint64_t>(user->get_id()));
		// A prior successful account save already committed the rewards.
		// The packet can be sent again without touching inventory or DNA.
		if (account.lastCashOutReceiptId == receiptId) {
			return true;
		}

		const uint32_t planets = mCashOutData.mPlanetsCompleted;
		const uint32_t stage = mChainData.GetLevelIndex();
		const std::size_t count = stage == 0 ? 0 :
			MissionResults::CashOutRewardCount(planets);
		const uint32_t level = MissionResults::CashOutRewardLevel(stage, planets);
		if (count > mCashOutData.mRewards[slot].size() || level > UINT16_MAX) {
			return false;
		}
		const MissionResults::RarityBoundaries bands {
			mCashOutData.mUpperRarityBoundaries[slot],
			mCashOutData.mLowerRarityBoundaries[slot]
		};
		std::array<CashOutData::Reward, 4> receiptItems {};
		std::vector<SporeNet::Part> parts;
		parts.reserve(count);
		for (std::size_t i = 0; i < count; ++i) {
			const auto roll = MissionResults::CashOutRewardRoll(receiptId, i);
			const auto rarity = MissionResults::CashOutRewardRarity(roll, bands);
			if (rarity == 0 || roll == 0) {
				return false;
			}
			// Interim valid rigblocks, selected deterministically from the
			// supported range. Class/element-specific source tables remain
			// to be extracted from the original asset database.
			SporeNet::Part part(MissionResults::CashOutRigblock(receiptId, i));
			part.SetLevel(static_cast<uint16_t>(level));
			part.SetRarity(static_cast<SporeNet::PartRarity>(rarity));
			part.SetCreationDate(utils::get_unix_time());
			receiptItems[i] = {
				part.GetRigblockAssetHash(),
				part.GetSuffixAssetHash(),
				part.GetPrefixAssetHash(),
				part.GetPrefixSecondaryAssetHash(),
				static_cast<int32_t>(level),
				static_cast<int32_t>(rarity),
				static_cast<int32_t>(roll)
			};
			parts.emplace_back(std::move(part));
		}

		const auto previousReceiptId = account.lastCashOutReceiptId;
		const auto previousDna = account.dna;
		const auto initialPartCount = user->get_parts().size();
		const auto experience = account.xp;
		account.lastCashOutReceiptId = receiptId;
		account.dna = MissionResults::AddDna(account.dna, mCashOutData.mDna);
		for (const auto& part : parts) {
			user->AddPart(part);
		}
		if (!user->Save()) {
			// One atomic disk snapshot: never consume a receipt if the
			// inventory/account write fails.
			account.lastCashOutReceiptId = previousReceiptId;
			account.dna = previousDna;
			user->get_parts().data().resize(initialPartCount);
			return false;
		}
		// Publish only a successful durable receipt to the client.
		mCashOutData.mRewards[slot] = receiptItems;
		mCashOutData.mStartingExperiences[slot] = experience;
		mCashOutData.mFinalExperiences[slot] = account.xp;
		return true;
	}

	void Instance::BeamOut(const PlayerPtr& player) {
		if (!player || !mServer || !mGameStarted || !mLevelLoaded) {
			return;
		}
		const auto& client = mServer->GetClient(player->GetId());
		if (client) {
			// Count each player's final medals once per dungeon even if the
			// client resends the completion status before transitioning.
			if (!mScoredPlayers.insert(player->GetId()).second) {
				return;
			}
			// Build final scores for this particular player. Global progress
			// stays separate from per-player hit counters.
			const auto objectives = GetObjectivesForPlayer(player->GetId(), true);
			// Inform the client about final objectives before it leaves the
			// dungeon. Cash-out data is handled by the subsequent vote.
			mServer->SendObjectivesComplete(client);
			// A repeated beam-out status packet must not count the same planet
			// multiple times. The run is limited to five planets.
			if (!mChainData.IsCompleted()) {
				mChainData.SetProgression(Progression::CompletePlanet(mChainData.GetProgression()));
				mChainData.SetCompleted(true);
			}
			// No verified formula for extra DNA at mission cash-out.
			// Regular loot is already persisted on pickup. Do not generate
			// invented DNA whenever a client reports successful beam-out.
			mCashOutData.mDna = 0;
			mCashOutData.mPlanetsCompleted = mChainData.GetProgression();
			// Associate each of the four wire slots with a player index.
			// This slot mapping still needs validation against client captures;
			// the medal counts themselves come from real objective results.
			if (player->GetId() < mCashOutData.mGoldMedals.size()) {
				const auto index = player->GetId();
				const auto totals = MissionResults::CountMedals(
					objectives, [](const auto& obj) { return static_cast<uint8_t>(obj.medal); });
				mCashOutData.mGoldMedals[index] += totals.gold;
				mCashOutData.mSilverMedals[index] += totals.silver;
				mCashOutData.mBronzeMedals[index] += totals.bronze;
				const auto bands = MissionResults::CashOutRarityBoundaries(
					mChainData.GetLevelIndex(), mCashOutData.mPlanetsCompleted,
					{mCashOutData.mGoldMedals[index],
					 mCashOutData.mSilverMedals[index],
					 mCashOutData.mBronzeMedals[index]});
				mCashOutData.mUpperRarityBoundaries[index] = bands.upper;
				mCashOutData.mLowerRarityBoundaries[index] = bands.lower;
			}
			// Item reward generation is still missing. The player receives
			// accurate rarity boundaries but no fabricated item-card records.

			mServer->SendReconnectPlayer(client, GameState::ChainVoting);
			mServer->SendDebugPing(client);
		}

		/*
		ClientEvent cashOutEvent;
		cashOutEvent.SetEventId(ClientEventID::PlayerCashOut);

		SendServerEvent(player, cashOutEvent);
		*/
	}

	/*
		HealthOrb.noun - Orb_Pickup
		HealthOrbPlaced.noun - Orb_Pickup
		JuggernautOrb.noun - Juggernaut_Pickup
		Loot_drop.noun - PickupLoot
		loot_questCrystal.noun - PickupQuestCrystal
		ManaOrb.noun - Orb_Pickup
		ManaOrbPlaced.noun - Orb_Pickup
		ResurrectOrb.noun - PickupResurrectOrb
		DNA.noun - DNA_Pickup
	*/

	bool Instance::SendObjectCreate(const ObjectPtr& object) {
		if (!object) {
			return false;
		}
#if 0
		for (const auto& [_, client] : mServer->GetClients()) {
			mServer->SendObjectCreate(client, object);
		}
#else
		if (!mGameStarted) {
			return false;
		}

		// Record real NPC spawns once; multiple clients may request the
		// same create packet, and hero objects are not enemy objectives.
		const auto& noun = object->GetNoun();
		if (noun && noun->GetNonPlayerClassData() && !object->IsPlayerControlled()) {
			mEnemyClearTracker.RegisterSpawn(object->GetId());
		}

		auto flags = object->GetFlags();
		if (flags & Object::Created) {
			return true;
		}

		for (const auto& [_, client] : mServer->GetClients()) {
			mServer->SendObjectCreate(client, object);
		}

		object->SetFlags(flags | Object::Created);
		object->ResetUpdateBits();
#endif
		return true;
	}

	void Instance::SendObjectDelete(const ObjectPtr& object) {
		for (const auto& [_, client] : mServer->GetClients()) {
			mServer->SendObjectDelete(client, object);
		}
	}

	void Instance::SendObjectDelete(const std::vector<ObjectPtr>& objects) {
		for (const auto& [_, client] : mServer->GetClients()) {
			mServer->SendObjectDelete(client, objects);
		}
	}

	void Instance::SendObjectUpdate(const ObjectPtr& object) {
		if (!SendObjectCreate(object)) {
			return;
		}

		auto flags = object->GetFlags();
		auto newFlags = flags;

		for (const auto& [_, client] : mServer->GetClients()) {
			if (!client) {
				continue;
			}
			const auto& player = client->GetPlayer();
			if (player && object == player->GetDeployedCharacterObject()) {
				player->SyncCharacterData();
			}

			if (object->mDataBits.any()) {
				mServer->SendObjectUpdate(client, object);
			}

			if (flags & Object::UpdateCombatant) {
				mServer->SendCombatantDataUpdate(client, object, *object->GetCombatantData());
				newFlags &= ~Object::UpdateCombatant;
			}

			if (flags & Object::UpdateAttributes) {
				mServer->SendAttributeDataUpdate(client, object, *object->GetAttributeData());
				newFlags &= ~Object::UpdateAttributes;
			}

			if (flags & Object::UpdateLootData) {
				mServer->SendLootDataUpdate(client, object, *object->GetLootData());
				newFlags &= ~Object::UpdateLootData;
			}

			if (flags & Object::UpdateAgentBlackboardData) {
				mServer->SendAgentBlackboardUpdate(client, object, *object->GetAgentBlackboardData());
				newFlags &= ~Object::UpdateAgentBlackboardData;
			}

			if (flags & Object::UpdateInteractableData) {
				mServer->SendInteractableDataUpdate(client, object, *object->GetInteractableData());
				newFlags &= ~Object::UpdateInteractableData;
			}

			if (flags & Object::UpdateLocomotion) {
				const auto& locomotion = object->GetLocomotionData();
				if (object->IsPlayerControlled()) {
					mServer->SendObjectPlayerMove(client, object, *locomotion);
				} else {
					if (locomotion->GetGoalFlags() & 0x20) {
						mServer->SendObjectTeleport(client, object, object->GetPosition(), locomotion->GetFacing());
					} else {
#if 0 // If unreliable update
						mServer->SendLocomotionDataUnreliableUpdate(client, object, locomotion->GetGoalPosition());
#else
						mServer->SendLocomotionDataUpdate(client, object, *locomotion);
#endif
					}
				}
				newFlags &= ~Object::UpdateLocomotion;
			}
		}

		object->SetFlags(newFlags);
		object->ResetUpdateBits();
	}

	void Instance::SendAnimationState(const ObjectPtr& object, uint32_t state, bool overlay, float scale) {
		if (!object) {
			return;
		}

		const auto timestamp = GetTime();
		if (!overlay) {
			object->mLastAnimationState = state;
			object->mLastAnimationPlayTime = timestamp;
		}

		for (const auto& [_, client] : mServer->GetClients()) {
			mServer->SendAnimationState(client, object, state, timestamp, overlay, scale);
		}
	}

	void Instance::SendObjectGfxState(const ObjectPtr& object, uint32_t state) {
		if (!object) {
			return;
		}

		const auto timestamp = GetTime();
		object->mGraphicsState = state;
		object->mGraphicsStateStartTime = timestamp;

		for (const auto& [_, client] : mServer->GetClients()) {
			mServer->SendObjectGfxState(client, object, state, timestamp);
		}
	}

	void Instance::SendServerEvent(const ServerEventBase& serverEvent) {
		if (!serverEvent) {
			return;
		}

		for (const auto& [_, client] : mServer->GetClients()) {
			mServer->SendServerEvent(client, serverEvent);
		}
	}

	void Instance::SendServerEvent(const PlayerPtr& player, const ServerEventBase& serverEvent) {
		if (!serverEvent || !player) {
			return;
		}

		const auto& client = mServer->GetClient(player->GetId());
		if (client) {
			mServer->SendServerEvent(client, serverEvent);
		}
	}

	void Instance::SendCombatEvent(const CombatEvent& combatEvent) {
		for (const auto& [_, client] : mServer->GetClients()) {
			mServer->SendCombatEvent(client, combatEvent);
		}
	}

	void Instance::SendCooldownUpdate(const ObjectPtr& object, uint32_t id, uint64_t start, uint32_t duration) {
		if (!object) {
			return;
		}

		for (const auto& [_, client] : mServer->GetClients()) {
			mServer->SendCooldownUpdate(client, object, id, start, duration);
		}
	}

	void Instance::SendLabsPlayerUpdate(const PlayerPtr& player) {
		if (!player || !player->NeedUpdate()) {
			return;
		}

		for (const auto& [_, client] : mServer->GetClients()) {
			mServer->SendLabsPlayerUpdate(client, player);
		}

		player->ResetUpdateBits();
	}

	void Instance::PickupLoot(const PlayerPtr& player, const ObjectPtr& object) {
		if (!player || !object || object->IsMarkedForDeletion()) {
			return;
		}
		const auto& lootData = object->GetLootData();
		if (!lootData) {
			return;
		}

		// Equipment and DNA pickups must be committed atomically before
		// the loot is consumed. The amount comes from server-owned LootData,
		// never from an untrusted client packet.
		const auto* part = lootData->GetPart();
		const float dnaAmount = lootData->GetDNAAmount();
		if (part || dnaAmount != 0.f) {
			const auto& user = player->GetUser();
			if (!user) {
				return;
			}
			auto& account = user->get_account();
			const auto previousDna = account.dna;
			if (dnaAmount != 0.f) {
				const auto reward = MissionResults::DecodeDnaPickup(dnaAmount);
				if (!reward) {
					return;
				}
				account.dna = MissionResults::AddDna(previousDna, *reward);
			}
			if (part) {
				user->AddPart(*part);
			}
			if (!user->Save()) {
				// Preserve the world loot and restore both in-memory balances.
				account.dna = previousDna;
				if (part) {
					user->get_parts().data().pop_back();
				}
				return;
			}
		}
		object->MarkForDeletion();

		ClientEvent lootEvent;
		lootEvent.SetLootPickup(player->GetId(), *lootData);
		for (const auto& [_, client] : mServer->GetClients()) {
			mServer->SendServerEvent(client, lootEvent);
		}
	}

	void Instance::PickupCatalyst(const PlayerPtr& player, const ObjectPtr& object) {
		if (!player || !object || object->IsMarkedForDeletion()) {
			return;
		}

		uint32_t slot = player->GetOpenCatalystSlot();
		if (slot != 0xFFFFFFFF) {
			const auto& lootData = object->GetLootData();
			if (!lootData) {
				// Actual object contains no catalyst, ignore without deleting it.
				return;
			}
			object->MarkForDeletion();

			auto catalyst = Catalyst(*lootData);
			player->SetCatalyst(catalyst, slot);

			const auto& client = mServer->GetClient(player->GetId());
			mServer->SendCrystalMessage(client, 0, 0, slot, catalyst);
		} else {
			// TODO: add bounce
			ClientEvent catalystEvent;
			catalystEvent.SetCatalystPickup(player->GetId());

			for (const auto& [_, client] : mServer->GetClients()) {
				mServer->SendServerEvent(client, catalystEvent);
			}
		}
	}
}

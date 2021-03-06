//
//  ScoreManager.cpp
//  SpaceExplorer
//
//  Created by João Baptista on 03/05/15.
//
//

#include "ScoreManager.h"
#include "cocos2d.h"
#include "Defaults.h"
#include "ExampleScoreManager.h"
#include "FacebookManager.h"
#include <algorithm>
#include <iterator>

#if CC_TARGET_PLATFORM == CC_PLATFORM_IOS
#include "GameCenterManager.h"
#elif CC_TARGET_PLATFORM == CC_PLATFORM_ANDROID
#include "GPGManager.h"
#endif

ScoreManager::TimeConstraint ScoreManager::currentTimeConstraint = ScoreManager::TimeConstraint::DAILY;
ScoreManager::SocialConstraint ScoreManager::currentSocialConstraint = ScoreManager::SocialConstraint::GLOBAL;
ScoreManager::Source ScoreManager::currentSource = ScoreManager::Source::FACEBOOK;

void ScoreManager::init()
{
#if CC_TARGET_PLATFORM == CC_PLATFORM_IOS
    currentSource = Source::PLATFORM_SPECIFIC;
    currentTimeConstraint = TimeConstraint::DAILY;
    currentSocialConstraint = SocialConstraint::GLOBAL;
#else
    currentSource = Source::FACEBOOK;
    currentTimeConstraint = TimeConstraint::ALL;
    currentSocialConstraint = SocialConstraint::FRIENDS;
#endif
}

void ScoreManager::loadPlayerCurrentScore(std::function<void(const ScoreData&)> handler)
{
    switch (currentSource)
    {
        //case Source::EXAMPLE: ExampleScoreManager::loadPlayerCurrentScore(handler); break;
        case Source::PLATFORM_SPECIFIC:
#if CC_TARGET_PLATFORM == CC_PLATFORM_IOS
            GameCenterManager::loadPlayerCurrentScore(handler); break;
#elif CC_TARGET_PLATFORM == CC_PLATFORM_ANDROID
            GPGManager::loadPlayerCurrentScore(handler); break;
#endif
        case Source::FACEBOOK: FacebookManager::loadPlayerCurrentScore(handler); break;
        default: break;
    }
}

void ScoreManager::loadHighscoresOnRange(long first, long last, std::function<void(long, std::vector<ScoreData>&&, std::string)> handler)
{
    switch (currentSource)
    {
        case Source::PLATFORM_SPECIFIC:
#if CC_TARGET_PLATFORM == CC_PLATFORM_IOS
            GameCenterManager::loadHighscoresOnRange(currentSocialConstraint, currentTimeConstraint, first, last, handler); break;
#elif CC_TARGET_PLATFORM == CC_PLATFORM_ANDROID
            GPGManager::loadHighscoresOnRange(currentSocialConstraint, currentTimeConstraint, first, last, handler); break;
#endif
        case Source::FACEBOOK: FacebookManager::loadHighscoresOnRange(currentSocialConstraint, currentTimeConstraint, first, last, handler); break;
        default: break;
    }
}

void ScoreManager::reportScore()
{
    AdditionalContext context = { (int32_t)global_GameTime, int32_t(global_MaxMultiplier * 2), int32_t(global_ShipSelect + 1) };
    
#if CC_TARGET_PLATFORM == CC_PLATFORM_IOS
    GameCenterManager::reportScore(global_GameScore, context);
#elif CC_TARGET_PLATFORM == CC_PLATFORM_ANDROID
    GPGManager::reportScore(global_GameScore, context);
#endif
    FacebookManager::reportScore(global_GameScore);
}

struct CompareScores
{
    bool operator()(const ScoreManager::ScoreData &s1, const ScoreManager::ScoreData &s2) const { return s1.score < s2.score; }
};

static std::mutex scoreBuildLock;
static std::set<ScoreManager::ScoreData, CompareScores> scoreTracking, tempBuildScore;
static uint64_t sourcesFetched;
static ScoreManager::ScoreData savedScore;

inline void fetchScores(ScoreManager::Source source, long position, std::vector<ScoreManager::ScoreData> &&data, std::string error)
{
	scoreBuildLock.lock();

    sourcesFetched |= 1 << (uint8_t)source;
    std::move(data.begin(), data.end(), std::inserter(tempBuildScore, tempBuildScore.end()));
    
    if (ScoreManager::trackedScoresReady()) scoreTracking = std::move(tempBuildScore);
    
    ScoreManager::ScoreData lastData;
    bool foundPlayer = false;
    
    for (auto it = scoreTracking.begin(); it != scoreTracking.end();)
    {
        if (it->isPlayer)
        {
            lastData = *it;
            foundPlayer = true;
            it = scoreTracking.erase(it);
        }
        else it++;
    }
    
    lastData.name = "Your maximum score";
    
    if (foundPlayer)
        scoreTracking.insert(std::move(lastData));
    
	scoreBuildLock.unlock();
}

void ScoreManager::updateScoreTrackingArray()
{
    sourcesFetched = 0;
    savedScore = ScoreData();
    
#if CC_TARGET_PLATFORM == CC_PLATFORM_IOS
    GameCenterManager::loadHighscoresOnRange(SocialConstraint::FRIENDS, TimeConstraint::ALL, 1, INT32_MAX, CC_CALLBACK_3(fetchScores, Source::PLATFORM_SPECIFIC), false);
#elif CC_TARGET_PLATFORM == CC_PLATFORM_ANDROID
	GPGManager::loadHighscoresOnRange(SocialConstraint::FRIENDS, TimeConstraint::ALL, 1, INT32_MAX, CC_CALLBACK_3(fetchScores, Source::PLATFORM_SPECIFIC), false);
#endif
    FacebookManager::loadHighscoresOnRange(SocialConstraint::FRIENDS, TimeConstraint::ALL, 1, INT32_MAX, CC_CALLBACK_3(fetchScores, Source::FACEBOOK), false);
}

bool ScoreManager::trackedScoresReady()
{
    return sourcesFetched == (1 << (uint8_t)ScoreManager::Source::NUMBER_OF_SOURCES) - 1;
}

ScoreManager::ScoreData ScoreManager::getNextTrackedScore(int64_t score)
{
    if (score >= savedScore.score)
	{
		scoreBuildLock.lock();

		auto it = scoreTracking.upper_bound(ScoreData(0, "", score));
		if (it == scoreTracking.end()) savedScore = ScoreData(-1, "", INT64_MAX);
		else savedScore = *it;

		scoreBuildLock.unlock();
	}
    
    return savedScore;
}

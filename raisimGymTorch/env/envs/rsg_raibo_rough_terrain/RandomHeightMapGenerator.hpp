//
// Created by jemin on 2/25/20.
//

#ifndef _RAISIM_GYM_ANYMAL_RAISIMGYM_ENV_ANYMAL_ENV_RANDOMHEIGHTMAPGENERATOR_HPP_
#define _RAISIM_GYM_ANYMAL_RAISIMGYM_ENV_ANYMAL_ENV_RANDOMHEIGHTMAPGENERATOR_HPP_

#include "raisim/World.hpp"

namespace raisim {

class RandomHeightMapGenerator {
 public:

  enum class GroundType : int {
    HEIGHT_MAP = 0,
    HEIGHT_MAP_DISCRETE = 1,
    STEPS = 2,
    STAIRS = 3
  };

  RandomHeightMapGenerator() = default;
  
  void setSeed(int seed) {
    terrain_seed_ = seed;
  }

  raisim::HeightMap* generateTerrain(raisim::World* world,
      GroundType groundType,
      double curriculumFactor,
      std::mt19937& gen,
      std::uniform_real_distribution<double>& uniDist) {
    std::vector<double> heightVec;
    heightVec.resize(heightMapSampleSize_*heightMapSampleSize_);
    std::unique_ptr<raisim::TerrainGenerator> genPtr;
    const double targetRoughness = 1.;

    switch (groundType) {
      case GroundType::HEIGHT_MAP:
        terrainProperties_.frequency = 0.8;
        terrainProperties_.zScale = targetRoughness * curriculumFactor;
        terrainProperties_.xSize = 12.0;
        terrainProperties_.ySize = 12.0;
        terrainProperties_.xSamples = 60;
        terrainProperties_.ySamples = 60;
        terrainProperties_.fractalOctaves = 5;
        terrainProperties_.fractalLacunarity = 3.0;
        terrainProperties_.fractalGain = 0.45;
        terrainProperties_.seed = terrain_seed_;
        terrain_seed_ += 500;
        terrainProperties_.stepSize = 0.;
        genPtr = std::make_unique<raisim::TerrainGenerator>(terrainProperties_);
        heightVec = genPtr->generatePerlinFractalTerrain();
        return world->addHeightMap(60, 60, 12.0, 12.0, 0., 0., heightVec);

      case GroundType::HEIGHT_MAP_DISCRETE:
        terrainProperties_.frequency = 0.3;
        terrainProperties_.zScale = targetRoughness * curriculumFactor * 1.2;
        terrainProperties_.xSize = 12.0;
        terrainProperties_.ySize = 12.0;
        terrainProperties_.xSamples = 80;
        terrainProperties_.ySamples = 80;
        terrainProperties_.fractalOctaves = 3;
        terrainProperties_.fractalLacunarity = 3.0;
        terrainProperties_.fractalGain = 0.45;
        terrainProperties_.seed = terrain_seed_++;
        terrainProperties_.stepSize = 0.1 * curriculumFactor;
        genPtr = std::make_unique<raisim::TerrainGenerator>(terrainProperties_);
        heightVec = genPtr->generatePerlinFractalTerrain();

        return world->addHeightMap(80, 80, 12.0, 12.0, 0., 0., heightVec);

      case GroundType::STEPS:

        heightVec.resize(120*120);
        for(int xBlock = 0; xBlock < 15; xBlock++) {
          for(int yBlock = 0; yBlock < 15; yBlock++) {
            double height = 0.1 * uniDist(gen) * curriculumFactor;
            for(int i=0; i<8; i++) {
              for(int j=0; j<8; j++) {
                heightVec[120 * (8*xBlock+i) + (8*yBlock+j)] = height + xBlock * targetRoughness * 0.25 * curriculumFactor;
              }
            }
          }
        }

        return world->addHeightMap(120, 120, 12.0, 12.0, 0., 0., heightVec);

      case GroundType::STAIRS:
        heightVec.resize(200*200);
        for(int xBlock = 0; xBlock < 25; xBlock++) {
          for(int i=0; i<200*200/25; i++) {
            heightVec[xBlock*200*200/25 + i] = xBlock * targetRoughness * 0.17 * curriculumFactor;
          }
        }
        return world->addHeightMap(200, 200, 12., 12., 0., 0., heightVec);
    }
    return nullptr;
  }

 private:
  raisim::TerrainProperties terrainProperties_;
  int heightMapSampleSize_ = 120;
  int terrain_seed_;
};

}

#endif //_RAISIM_GYM_ANYMAL_RAISIMGYM_ENV_ANYMAL_ENV_RANDOMHEIGHTMAPGENERATOR_HPP_

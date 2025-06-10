#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstdint>
#include <glm/glm.hpp>

#define MAX_PLAYERS 10
#define MAX_PROJECTILES 100
#define MAP_WIDTH 40
#define MAP_HEIGHT 10
#define MAP_LENGTH 40

enum VoxelType {
    AIR = 0,
    SOLID = 1
};

enum PacketType {
    JOIN,
    JOIN_ACK,
    ACT,
    STATE,
    MAP_DATA,
    SOUND_EVENT,
    LEAVE
};

enum MovementDirection : uint8_t {
    FORWARD = 0,
    FORWARD_LEFT = 1,
    LEFT = 2,
    BACKWARDS_LEFT = 3,
    BACKWARDS = 4,
    BACKWARDS_RIGHT = 5,
    RIGHT = 6,
    FORWARD_RIGHT = 7,
    NONE = 8
};


enum SoundType {
    GUNSHOT,
    FOOTSTEP
};

#pragma pack(push, 1)
struct Point { 
    int x, y;
};

struct ProtoHeader {
    uint8_t type;
    uint32_t tick_id;
};

struct JoinAckPacket {
    ProtoHeader hdr;
    uint32_t your_id;
};

struct ActionPacket {
    ProtoHeader hdr;
    glm::vec3 pos;
    glm::vec3 view_dir;
    MovementDirection movement_dir;
    uint8_t is_firing;
    uint8_t is_jumping;
};

struct PlayerState {
    uint32_t player_id;
    glm::vec3 pos;
    glm::vec3 view_dir;
    MovementDirection movement_dir;
    uint8_t is_alive;
    uint8_t on_ground;
};

struct ProjectileState {
    bool is_active;
    uint32_t owner_id;
    glm::vec3 pos;
    glm::vec3 dir;
};

struct StatePacket {
    ProtoHeader hdr;
    uint8_t num_players;
    PlayerState players[MAX_PLAYERS];
    int num_projectiles;
    ProjectileState projectiles[MAX_PROJECTILES];
};

struct MapPacket {
    ProtoHeader hdr;
    int map[MAP_WIDTH][MAP_HEIGHT][MAP_LENGTH];
};

struct SoundEventPacket {
    ProtoHeader hdr;
    SoundType sound_type;
    glm::vec3 pos;
};
#pragma pack(pop)


#endif
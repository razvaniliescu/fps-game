#include <iostream>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <unordered_map>
#include <vector>
#include <string>
#include <sstream>
#include <chrono>
#include <cmath>
#include <glm/glm.hpp>

#include "protocol.h"

using namespace std;
using Clock = chrono::steady_clock;

#define MAX_EVENTS 100
#define BUFLEN 1024

struct ClientInfo {
    sockaddr_in addr;
    PlayerState state;
    Clock::time_point last_fire_time;
    Clock::time_point respawn_time;
    Clock::time_point last_packet_time;
    string client_key;
    glm::vec3 pos_at_last_step;
    float velocityY = 0.0f;
};

unordered_map<uint32_t, ClientInfo> clients;
unordered_map<string, uint32_t> addr_to_id;
int game_map[MAP_WIDTH][MAP_HEIGHT][MAP_LENGTH];
ProjectileState projectiles[MAX_PROJECTILES];

uint32_t next_player_id = 1;
uint32_t current_tick = 0;

const float PLAYER_HEIGHT = 0.9f;
const float PLAYER_SPEED = 3.5f;
const float PROJECTILE_SPEED = 100.0f;
const float PLAYER_RADIUS = 0.3f;
const float PROJECTILE_RADIUS = 0.05f;
const int FIRE_COOLDOWN_MS = 200;
const int CLIENT_TIMEOUT_S = 15;
const float STEP_DISTANCE = 2.0f;
const float GRAVITY = -9.8f;
const float JUMP_POWER = 5.0f;
const float MAX_STEP_HEIGHT = 1.1f;

struct Point3D { 
    int x, y, z; 
};

struct Room {
    int x, y, z, width, height, length;
    bool intersects(const Room& other) const { 
        return (x < other.x + other.width && x + width > other.x 
            && z < other.z + other.length && z + length > other.z); 
    }
    Point3D center() const { return {x + width / 2, y, z + length / 2}; }
};

std::vector<glm::vec3> spawn_points;

void create_room_3d(int x, int y, int z, int width, int height, int length) {
    for (int i = x; i < x + width; i++) {
        for (int j = y; j < y + height; j++) {
            for (int k = z; k < z + length; k++) {
                if (i > 0 && i < MAP_WIDTH - 1 && j > 0 && j < MAP_HEIGHT - 1 && k > 0 && k < MAP_LENGTH - 1) {
                    if (game_map[i][j][k] == SOLID) {
                        game_map[i][j][k] = AIR;
                        if (j == y) {
                            spawn_points.push_back({(float)i + 0.5f, (float)j + 0.5f, (float)k + 0.5f});
                        }
                    }
                }
            }
        }
    }
}

void create_ramp(Point3D start, Point3D end, int width) {
    glm::vec3 p1 = {(float)start.x, (float)start.y, (float)start.z};
    glm::vec3 p2 = {(float)end.x, (float)end.y, (float)end.z};
    float dist = glm::distance(p1, p2);
    if (dist == 0.0f) return;
    glm::vec3 dir = glm::normalize(p2 - p1);

    for (float i = 0; i < dist; i += 0.5f) {
        glm::vec3 current_pos = p1 + dir * i;
        int map_x = (int)current_pos.x;
        int map_y = (int)current_pos.y;
        int map_z = (int)current_pos.z;

        for (int w = -width / 2; w <= width / 2; ++w) {
            for (int fill_y = 0; fill_y <= map_y; ++fill_y) {
                if (map_x >= 0 && map_x < MAP_WIDTH && fill_y >= 0 && fill_y < MAP_HEIGHT && (map_z + w) >= 0 && (map_z + w) < MAP_LENGTH) {
                    game_map[map_x][fill_y][map_z + w] = SOLID;
                }
            }
            if (map_x >= 0 && map_x < MAP_WIDTH && (map_y + 1) < MAP_HEIGHT && (map_z + w) >= 0 && (map_z + w) < MAP_LENGTH) {
                game_map[map_x][map_y + 1][map_z + w] = AIR;
                game_map[map_x][map_y + 2][map_z + w] = AIR;
            }
        }
    }
}

void create_h_tunnel_3d(int x1, int x2, int y, int z) {
    for (int x = std::min(x1, x2); x <= std::max(x1, x2); x++) {
        game_map[x][y][z] = AIR;
        game_map[x][y+1][z] = AIR;
        game_map[x][y-1][z] = SOLID;
    }
}

void create_v_tunnel_3d(int z1, int z2, int y, int x) {
    for (int z = std::min(z1, z2); z <= std::max(z1, z2); z++) {
        game_map[x][y][z] = AIR;
        game_map[x][y+1][z] = AIR;
        game_map[x][y-1][z] = SOLID;
    }
}

std::vector<Room> generate_level(int y_level, long unsigned int min_rooms, int room_height) {
    std::vector<Room> rooms;
    int max_attempts = 100;
    int attempts = 0;

    while (rooms.size() < min_rooms && attempts < max_attempts) {
        attempts++;
        int w = 6 + rand() % 7;
        int l = 6 + rand() % 7;
        int x = rand() % (MAP_WIDTH - w - 1) + 1;
        int z = rand() % (MAP_LENGTH - l - 1) + 1;

        Room new_room = {x, y_level, z, w, room_height, l};

        bool failed = false;
        for (const auto& other_room : rooms) {
            if (new_room.intersects(other_room)) {
                failed = true;
                break;
            }
        }

        if (!failed) {
            create_room_3d(new_room.x, new_room.y, new_room.z, new_room.width, new_room.height, new_room.length);
            rooms.push_back(new_room);
        }
    }
    return rooms;
}

void generate_map() {
    spawn_points.clear();
    for (int x = 0; x < MAP_WIDTH; x++) {
        for (int y = 0; y < MAP_HEIGHT; y++) {
            for (int z = 0; z < MAP_LENGTH; z++) {
                game_map[x][y][z] = SOLID;
            }
        }
    }

    auto level1_rooms = generate_level(1, 5, 4);
    auto level2_rooms = generate_level(6, 5, 4);

    for (size_t i = 0; i < level1_rooms.size() - 1; i++) {
        Point3D center1 = level1_rooms[i].center();
        Point3D center2 = level1_rooms[i+1].center();
        create_h_tunnel_3d(center1.x, center2.x, 1, center2.z);
        create_v_tunnel_3d(center1.z, center2.z, 1, center1.x);
    }
     for (size_t i = 0; i < level2_rooms.size() - 1; i++) {
        Point3D center1 = level2_rooms[i].center();
        Point3D center2 = level2_rooms[i+1].center();
        create_h_tunnel_3d(center1.x, center2.x, 6, center2.z);
        create_v_tunnel_3d(center1.z, center2.z, 6, center1.x);
    }

    if (level1_rooms.empty() || level2_rooms.empty()) {
        cout << "Map could be unconnected" << endl;
        return;
    }

    Point3D ramp1_start = level1_rooms[rand() % level1_rooms.size()].center();
    Point3D ramp1_end = level2_rooms[rand() % level2_rooms.size()].center();
    create_ramp(ramp1_start, ramp1_end, 3);

    Point3D ramp2_start = level1_rooms[rand() % level1_rooms.size()].center();
    Point3D ramp2_end = level2_rooms[rand() % level2_rooms.size()].center();
    create_ramp(ramp2_start, ramp2_end, 3);
}

void spawn_projectile(uint32_t owner_id, const glm::vec3& pos, const glm::vec3& dir) {
    for (int i = 0; i < MAX_PROJECTILES; ++i) {
        if (!projectiles[i].is_active) {
            projectiles[i].is_active = true;
            projectiles[i].owner_id = owner_id;
            projectiles[i].pos = pos;
            projectiles[i].dir = dir;
            return;
        }
    }
}

bool check_line_sphere_collision(const glm::vec3& line_start, const glm::vec3& line_end, 
    const glm::vec3& sphere_center, float sphere_radius) {

    glm::vec3 line_dir = line_end - line_start;
    glm::vec3 to_sphere = sphere_center - line_start;

    float line_len_sq = glm::dot(line_dir, line_dir);
    if (line_len_sq == 0.0f) {
        return glm::length(to_sphere) < sphere_radius;
    }

    float t = glm::dot(to_sphere, line_dir) / line_len_sq;
    t = glm::clamp(t, 0.0f, 1.0f);
    glm::vec3 closest_point = line_start + t * line_dir;

    return glm::distance(closest_point, sphere_center) < sphere_radius;
}

int get_floor_height(int x, int z, int start_y) {
    if (x < 0 || x >= MAP_WIDTH || z < 0 || z >= MAP_LENGTH) return -1;
    for (int y = std::min(start_y, MAP_HEIGHT - 1); y >= 0; --y) {
        if (game_map[x][y][z] == SOLID) {
            return y;
        }
    }
    return -1;
}

void update_players(float dt, int udp_socket) {
    for (auto& [id, client] : clients) {
        if (!client.state.is_alive) continue;

        glm::vec3 initial_pos = client.state.pos;

        glm::vec2 move_input(0.0f, 0.0f);
        glm::vec2 view_dir_flat(client.state.view_dir.x, client.state.view_dir.z);
        if (glm::length(view_dir_flat) > 0.0f) {
            view_dir_flat = glm::normalize(view_dir_flat);
        }
        glm::vec2 right_dir(-view_dir_flat.y, view_dir_flat.x);

        switch (client.state.movement_dir) {
            case FORWARD:       move_input += view_dir_flat; break;
            case BACKWARDS:     move_input -= view_dir_flat; break;
            case LEFT:          move_input -= right_dir; break;
            case RIGHT:         move_input += right_dir; break;
            case FORWARD_LEFT:  move_input += view_dir_flat - right_dir; break;
            case FORWARD_RIGHT: move_input += view_dir_flat + right_dir; break;
            case BACKWARDS_LEFT:move_input -= view_dir_flat + right_dir; break;
            case BACKWARDS_RIGHT:move_input -= view_dir_flat - right_dir; break;
            default: break;
        }

        if (glm::length(move_input) > 0.0f) {
            glm::vec2 total_move = glm::normalize(move_input) * PLAYER_SPEED * dt;
            
            float next_x = client.state.pos.x + total_move.x;
            float next_z = client.state.pos.z + total_move.y;
            int head_y = (int)(client.state.pos.y + PLAYER_HEIGHT / 2.0f * 0.9f);
            
            if (game_map[(int)next_x][head_y][(int)client.state.pos.z] == AIR) {
                client.state.pos.x = next_x;
            }
             if (game_map[(int)client.state.pos.x][head_y][(int)next_z] == AIR) {
                client.state.pos.z = next_z;
            }
        }
        
        client.velocityY += GRAVITY * dt;
        client.state.pos.y += client.velocityY * dt;

        int map_x = (int)client.state.pos.x;
        int map_z = (int)client.state.pos.z;
        
        int head_y = (int)(client.state.pos.y + PLAYER_HEIGHT / 2.0f);
        if (head_y < MAP_HEIGHT) {
            if (game_map[map_x][head_y][map_z] == SOLID) {
                client.velocityY = 0;
                client.state.pos.y = (float)head_y - (PLAYER_HEIGHT / 2.0f) - 0.01f;
            }
        }

        int floor_y = (int)(client.state.pos.y - PLAYER_HEIGHT / 2.0f);
        if (floor_y >= 0) {
            int block_under = game_map[map_x][floor_y][map_z];
            if (block_under == SOLID) {
                if (client.velocityY <= 0) {
                    client.state.pos.y = (float)floor_y + 1.0f + PLAYER_HEIGHT / 2.0f;
                    client.velocityY = 0;
                    client.state.on_ground = true;
                }
            } else {
                client.state.on_ground = false;
            }
        } else {
             client.state.on_ground = false;
        }

        if (glm::distance(initial_pos, client.state.pos) > 0.001f && client.state.on_ground) {
            float distance_since_last_step = glm::distance(glm::vec2(client.state.pos.x, client.state.pos.z), glm::vec2(client.pos_at_last_step.x, client.pos_at_last_step.z));
            if (distance_since_last_step >= STEP_DISTANCE) {
                SoundEventPacket sound_pkt;
                sound_pkt.hdr.type = SOUND_EVENT;
                sound_pkt.sound_type = FOOTSTEP;
                sound_pkt.pos = client.state.pos;
                for (auto const& [pid, p_client] : clients) {
                    sendto(udp_socket, &sound_pkt, sizeof(sound_pkt), 0, (sockaddr*)&p_client.addr, sizeof(p_client.addr));
                }
                client.pos_at_last_step = client.state.pos;
            }
        }
    }
}

void update_projectiles(float dt) {
    for (int i = 0; i < MAX_PROJECTILES; ++i) {
        if (projectiles[i].is_active) {
            glm::vec3 previous_pos = projectiles[i].pos;

            projectiles[i].pos += projectiles[i].dir * PROJECTILE_SPEED * dt;
            int map_x = (int)projectiles[i].pos.x;
            int map_y = (int)projectiles[i].pos.y;
            int map_z = (int)projectiles[i].pos.z;

            if (map_x < 0 || map_x >= MAP_WIDTH || map_y < 0 || map_y >= MAP_HEIGHT || map_z < 0 || map_z >= MAP_LENGTH || game_map[map_x][map_y][map_z] == SOLID) {
                projectiles[i].is_active = false;
                continue;
            }
            for (auto& [id, client] : clients) {
                if (!client.state.is_alive || id == projectiles[i].owner_id) continue;
                float total_radius = PLAYER_RADIUS + PROJECTILE_RADIUS;

                if (check_line_sphere_collision(previous_pos, projectiles[i].pos, client.state.pos, total_radius)) {
                    client.state.is_alive = 0;
                    client.respawn_time = Clock::now() + std::chrono::seconds(3);
                    projectiles[i].is_active = false;
                    cout << "Player " << id << " was hit!" << endl;
                    break;
                }
            }
        }
    }
}

void respawn_player(ClientInfo& client) {
    client.state.is_alive = 1;
    client.velocityY = 0.0f;
    if (!spawn_points.empty()) {
        client.state.pos = spawn_points[rand() % spawn_points.size()];
        client.state.pos.y += PLAYER_HEIGHT / 2.0f;
    } else {
        client.state.pos = {5.0f, 1.5f, 5.0f};
    }
    client.pos_at_last_step = client.state.pos;
}

int main(int argc, char *argv[]) {
    if (argc < 2) { cerr << "Usage: " << argv[0] << " <port>\n"; return 1; }
    int port = atoi(argv[1]);
    char buf[BUFLEN];
    memset(projectiles, 0, sizeof(projectiles));
    int udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    bind(udp_socket, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    cout << "Server started on port " << port << endl;
    int epfd = epoll_create1(0);
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = udp_socket;
    epoll_ctl(epfd, EPOLL_CTL_ADD, udp_socket, &ev);
    struct epoll_event events[MAX_EVENTS];
    auto last_tick_time = Clock::now();
    const int tick_interval_ms = 33;
    srand(time(NULL));
    generate_map();

    while (true) {
        int nfds = epoll_wait(epfd, events, MAX_EVENTS, 5);
        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == udp_socket) {
                sockaddr_in client_addr;
                socklen_t len = sizeof(client_addr);
                size_t recv_len = recvfrom(udp_socket, buf, sizeof(buf), 0, (sockaddr*) &client_addr, &len);
                if (recv_len < sizeof(ProtoHeader)) continue;
                ProtoHeader *hdr = (ProtoHeader *)buf;
                ostringstream oss;
                oss << inet_ntoa(client_addr.sin_addr) << ":" << ntohs(client_addr.sin_port);
                string client_key = oss.str();
                if (hdr->type == JOIN) {
                    if (addr_to_id.count(client_key) == 0 && clients.size() < MAX_PLAYERS) {
                        uint32_t new_id = next_player_id++;
                        addr_to_id[client_key] = new_id;
                        ClientInfo new_client;
                        new_client.addr = client_addr;
                        new_client.state.player_id = new_id;
                        respawn_player(new_client);
                        new_client.last_fire_time = Clock::now();
                        new_client.last_packet_time = Clock::now();
                        new_client.client_key = client_key;
                        new_client.pos_at_last_step = new_client.state.pos;
                        clients[new_id] = new_client;
                        JoinAckPacket pkt;
                        pkt.hdr.type = JOIN_ACK;
                        pkt.your_id = new_id;
                        sendto(udp_socket, &pkt, sizeof(pkt), 0, (sockaddr*)&client_addr, len);
                        MapPacket map_pkt;
                        map_pkt.hdr.type = MAP_DATA;
                        memcpy(map_pkt.map, game_map, sizeof(game_map));
                        sendto(udp_socket, &map_pkt, sizeof(map_pkt), 0, (sockaddr*)&client_addr, len);
                        cout << "Player " << new_id << " joined from " << client_key << "\n";
                    }
                } else if (hdr->type == ACT) {
                    if (addr_to_id.count(client_key)) {
                        uint32_t id = addr_to_id[client_key];
                        if (clients.count(id) && recv_len >= sizeof(ActionPacket)) {
                            ActionPacket* pkt = (ActionPacket*)buf;
                            clients[id].state.movement_dir = pkt->movement_dir;
                            clients[id].state.view_dir = pkt->view_dir;
                            clients[id].last_packet_time = Clock::now();

                            if (pkt->is_jumping && clients[id].state.on_ground) {
                                clients[id].velocityY = JUMP_POWER;
                                clients[id].state.on_ground = false;
                            }

                            auto now = Clock::now();
                            if (pkt->is_firing && clients[id].state.is_alive &&
                                chrono::duration_cast<chrono::milliseconds>(now - clients[id].last_fire_time).count() >= FIRE_COOLDOWN_MS) {
                                clients[id].last_fire_time = now;
                                glm::vec3 spawn_pos = clients[id].state.pos;
                                spawn_pos.y += 0.2f; // Eye height offset
                                spawn_projectile(id, spawn_pos, pkt->view_dir);

                                SoundEventPacket sound_pkt;
                                sound_pkt.hdr.type = SOUND_EVENT;
                                sound_pkt.sound_type = GUNSHOT;
                                sound_pkt.pos = spawn_pos;

                                for (auto const& [pid, p_client] : clients) {
                                    sendto(udp_socket, &sound_pkt, sizeof(sound_pkt), 0, (sockaddr*)&p_client.addr, sizeof(p_client.addr));
                                }
                            }
                        }
                    }
                } else if (hdr->type == LEAVE) {
                    if (addr_to_id.count(client_key)) {
                        uint32_t id = addr_to_id[client_key];
                        cout << "Player " << id << " has left the game." << endl;
                        clients.erase(id);
                        addr_to_id.erase(client_key);
                    }
                }
            }
        }
        auto current_time = Clock::now();
        auto elapsed_ms = chrono::duration_cast<chrono::milliseconds>(current_time - last_tick_time).count();
        if (elapsed_ms >= tick_interval_ms) {
            last_tick_time = current_time;
            float dt = elapsed_ms / 1000.0f;
            std::vector<uint32_t> timed_out_ids;
            for (auto const& [id, client] : clients) {
                if (chrono::duration_cast<chrono::seconds>(current_time - client.last_packet_time).count() > CLIENT_TIMEOUT_S) {
                    timed_out_ids.push_back(id);
                }
            }

            for (uint32_t id : timed_out_ids) {
                cout << "Player " << id << " timed out. Removing." << endl;
                addr_to_id.erase(clients[id].client_key);
                clients.erase(id);
            }

            for (auto& [id, client] : clients) {
                if (!client.state.is_alive && current_time >= client.respawn_time) {
                    respawn_player(client);
                }
            }
            update_players(dt, udp_socket);
            update_projectiles(dt);
            StatePacket spkt{};
            spkt.hdr.type = STATE;
            spkt.hdr.tick_id = current_tick++;
            spkt.num_players = 0;
            for (auto const& [id, client] : clients) {
                if (spkt.num_players < MAX_PLAYERS) { spkt.players[spkt.num_players++] = client.state; }
            }
            spkt.num_projectiles = 0;
            for(int i = 0; i < MAX_PROJECTILES; ++i) {
                if (projectiles[i].is_active) {
                     if (spkt.num_projectiles < MAX_PROJECTILES) { spkt.projectiles[spkt.num_projectiles++] = projectiles[i]; }
                }
            }
            for (auto const& [id, client] : clients) {
                sendto(udp_socket, &spkt, sizeof(spkt), 0, (sockaddr*)&client.addr, sizeof(client.addr));
            }
        }
    }
    close(udp_socket);
    return 0;
}
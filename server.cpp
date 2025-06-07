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
};

unordered_map<uint32_t, ClientInfo> clients;
unordered_map<string, uint32_t> addr_to_id;
int game_map[MAP_WIDTH][MAP_LENGTH];
ProjectileState projectiles[MAX_PROJECTILES];

uint32_t next_player_id = 1;
uint32_t current_tick = 0;

const float PLAYER_SPEED = 3.5f;
const float PROJECTILE_SPEED = 25.0f;
const float PLAYER_RADIUS = 0.3f;
const float PROJECTILE_RADIUS = 0.05f;
const int FIRE_COOLDOWN_MS = 200;

struct Point { int x, y; };
struct Room {
    int x, y, width, length;
    bool intersects(const Room& other) const { return (x < other.x + other.width && x + width > other.x && y < other.y + other.length && y + length > other.y); }
    Point center() const { return {x + width / 2, y + length / 2}; }
};
std::vector<Point> spawn_points;

void create_h_tunnel(int x1, int x2, int z) {
    for (int x = std::min(x1, x2); x <= std::max(x1, x2); x++) {
        if (game_map[x][z] == WALL) { game_map[x][z] = FLOOR; spawn_points.push_back({x, z}); }
    }
}
void create_v_tunnel(int z1, int z2, int x) {
    for (int z = std::min(z1, z2); z <= std::max(z1, z2); z++) {
        if (game_map[x][z] == WALL) { game_map[x][z] = FLOOR; spawn_points.push_back({x, z}); }
    }
}
void generate_map() {
    spawn_points.clear();
    for (int x = 0; x < MAP_WIDTH; x++) {
        for (int y = 0; y < MAP_LENGTH; y++) { game_map[x][y] = WALL; }
    }
    std::vector<Room> rooms;
    int max_rooms = 10;
    int min_room_size = 6;
    int max_room_size = 12;
    for (int i = 0; i < max_rooms; i++) {
        int w = min_room_size + rand() % (max_room_size - min_room_size + 1);
        int h = min_room_size + rand() % (max_room_size - min_room_size + 1);
        int x = rand() % (MAP_WIDTH - w - 1) + 1;
        int y = rand() % (MAP_LENGTH - h - 1) + 1;
        Room new_room = {x, y, w, h};
        bool failed = false;
        for (const auto& other_room : rooms) { if (new_room.intersects(other_room)) { failed = true; break; } }
        if (!failed) {
            for (int rx = new_room.x; rx < new_room.x + new_room.width; rx++) {
                for (int ry = new_room.y; ry < new_room.y + new_room.length; ry++) {
                    if (game_map[rx][ry] == WALL) { game_map[rx][ry] = FLOOR; spawn_points.push_back({rx, ry}); }
                }
            }
            if (!rooms.empty()) {
                Point new_center = new_room.center();
                Point prev_center = rooms.back().center();
                if (rand() % 2 == 0) {
                    create_h_tunnel(prev_center.x, new_center.x, prev_center.y);
                    create_v_tunnel(prev_center.y, new_center.y, new_center.x);
                } else {
                    create_v_tunnel(prev_center.y, new_center.y, prev_center.x);
                    create_h_tunnel(prev_center.x, new_center.x, new_center.y);
                }
            }
            rooms.push_back(new_room);
        }
    }
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

    // Proiectăm vectorul de la începutul liniei la centrul sferei pe linia însăși
    float t = glm::dot(to_sphere, line_dir) / line_len_sq;

    // Limităm proiecția la segmentul de linie (între 0 și 1)
    t = glm::clamp(t, 0.0f, 1.0f);

    // Găsim cel mai apropiat punct de pe segmentul de linie față de centrul sferei
    glm::vec3 closest_point = line_start + t * line_dir;

    // Verificăm dacă distanța de la acest punct la centrul sferei este mai mică decât raza
    return glm::distance(closest_point, sphere_center) < sphere_radius;
}

void update_players(float dt) {
    for (auto& [id, client] : clients) {
        if (!client.state.is_alive) continue;

        glm::vec2 view_dir_flat(client.state.view_dir.x, client.state.view_dir.z);
        if (glm::length(view_dir_flat) > 0.0f) {
            view_dir_flat = glm::normalize(view_dir_flat);
        }
        glm::vec2 right_dir(-view_dir_flat.y, view_dir_flat.x);

        glm::vec2 move_input(0.0f, 0.0f);
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
            
            float potential_x = client.state.pos.x + total_move.x;
            float potential_z = client.state.pos.z + total_move.y;

            if (game_map[(int)(potential_x + (total_move.x > 0 ? PLAYER_RADIUS : -PLAYER_RADIUS))][(int)client.state.pos.z] == FLOOR) {
                client.state.pos.x = potential_x;
            }
            if (game_map[(int)client.state.pos.x][(int)(potential_z + (total_move.y > 0 ? PLAYER_RADIUS : -PLAYER_RADIUS))] == FLOOR) {
                client.state.pos.z = potential_z;
            }
        }
    }
}

void update_projectiles(float dt) {
    for (int i = 0; i < MAX_PROJECTILES; ++i) {
        if (projectiles[i].is_active) {
            glm::vec3 previous_pos = projectiles[i].pos;
            projectiles[i].pos += projectiles[i].dir * PROJECTILE_SPEED * dt;

            if (projectiles[i].pos.y < 0.0f || projectiles[i].pos.y > 5.0f) {
                projectiles[i].is_active = false; continue;
            }
            int map_x = (int)projectiles[i].pos.x;
            int map_z = (int)projectiles[i].pos.z;
            if (map_x < 0 || map_x >= MAP_WIDTH || map_z < 0 || map_z >= MAP_LENGTH || game_map[map_x][map_z] == WALL) {
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
                    cout << "Player " << id << " was hit by precise collision!" << endl;
                    break;
                }
            }
        }
    }
}

void respawn_player(ClientInfo& client) {
    client.state.is_alive = 1;
    if (!spawn_points.empty()) {
        int spawn_index = rand() % spawn_points.size();
        Point start_pos = spawn_points[spawn_index];
        client.state.pos = {(float)start_pos.x + 0.5f, 0.5f, (float)start_pos.y + 0.5f};
    } else {
        client.state.pos = {5.0f, 0.5f, 5.0f};
    }
    cout << "Player " << client.state.player_id << " has respawned!" << endl;
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
                        respawn_player(new_client); // Folosim funcția de respawn pentru a seta starea inițială
                        new_client.last_fire_time = Clock::now();
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
                            auto now = Clock::now();
                            if (pkt->is_firing && clients[id].state.is_alive &&
                                chrono::duration_cast<chrono::milliseconds>(now - clients[id].last_fire_time).count() >= FIRE_COOLDOWN_MS) {
                                clients[id].last_fire_time = now;
                                glm::vec3 spawn_pos = clients[id].state.pos;
                                spawn_pos.y += 0.2f; // Eye height offset
                                spawn_projectile(id, spawn_pos, pkt->view_dir);
                            }
                        }
                    }
                }
            }
        }
        auto current_time = Clock::now();

        auto elapsed_ms = chrono::duration_cast<chrono::milliseconds>(current_time - last_tick_time).count();
        if (elapsed_ms >= tick_interval_ms) {
            last_tick_time = current_time;
            float dt = elapsed_ms / 1000.0f;
            for (auto& [id, client] : clients) {
                if (!client.state.is_alive && current_time >= client.respawn_time) {
                    respawn_player(client);
                }
            }
            update_players(dt);
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
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <ctime>
#include <fcntl.h>
#include <chrono>
#include <unordered_set>
#include <GLFW/glfw3.h>
#define _USE_MATH_DEFINES
#include <math.h>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <chrono>

#include "protocol.h"

#define BUFLEN 1024

using Clock = std::chrono::steady_clock;

int game_map[MAP_WIDTH][MAP_LENGTH];
auto last_fire_time = Clock::now();
float cameraYaw   = 0.0f;
float cameraPitch = 0.0f;
double lastMouseX, lastMouseY;
bool firstMouse = true;
const float PLAYER_RADIUS = 0.3f;
const float PROJECTILE_RADIUS = 0.05f;

void draw_cube(float x, float y, float z, float height) {
    glPushMatrix();
    glTranslatef(x + 0.5f, y + height / 2.0f, z + 0.5f);
    glScalef(1.0f, height, 1.0f);
    glBegin(GL_QUADS);
        glColor3f(0.6f, 0.6f, 0.6f);
        glVertex3f( 0.5f, 0.5f, -0.5f); glVertex3f(-0.5f, 0.5f, -0.5f); glVertex3f(-0.5f, 0.5f,  0.5f); glVertex3f( 0.5f, 0.5f,  0.5f);
        glColor3f(0.4f, 0.4f, 0.4f);
        glVertex3f( 0.5f, -0.5f,  0.5f); glVertex3f(-0.5f, -0.5f,  0.5f); glVertex3f(-0.5f, -0.5f, -0.5f); glVertex3f( 0.5f, -0.5f, -0.5f);
        glColor3f(0.5f, 0.5f, 0.5f);
        glVertex3f( 0.5f,  0.5f, 0.5f); glVertex3f(-0.5f,  0.5f, 0.5f); glVertex3f(-0.5f, -0.5f, 0.5f); glVertex3f( 0.5f, -0.5f, 0.5f);
        glColor3f(0.5f, 0.5f, 0.5f);
        glVertex3f( 0.5f, -0.5f, -0.5f); glVertex3f(-0.5f, -0.5f, -0.5f); glVertex3f(-0.5f,  0.5f, -0.5f); glVertex3f( 0.5f,  0.5f, -0.5f);
        glColor3f(0.55f, 0.55f, 0.55f);
        glVertex3f(-0.5f,  0.5f,  0.5f); glVertex3f(-0.5f,  0.5f, -0.5f); glVertex3f(-0.5f, -0.5f, -0.5f); glVertex3f(-0.5f, -0.5f,  0.5f);
        glColor3f(0.55f, 0.55f, 0.55f);
        glVertex3f( 0.5f,  0.5f, -0.5f); glVertex3f( 0.5f,  0.5f,  0.5f); glVertex3f( 0.5f, -0.5f,  0.5f); glVertex3f( 0.5f, -0.5f, -0.5f);
    glEnd();
    glPopMatrix();
}

void draw_sphere(float radius, int sectors, int stacks) {
    for(int i = 0; i < stacks; ++i) {
        float phi1 = M_PI * ((float)i / stacks);
        float phi2 = M_PI * ((float)(i + 1) / stacks);
        glBegin(GL_QUAD_STRIP);
        for(int j = 0; j <= sectors; ++j) {
            float theta = 2 * M_PI * ((float)j / sectors);
            float x1 = radius * sin(phi1) * cos(theta); float y1 = radius * cos(phi1); float z1 = radius * sin(phi1) * sin(theta);
            glVertex3f(x1, y1, z1);
            float x2 = radius * sin(phi2) * cos(theta); float y2 = radius * cos(phi2); float z2 = radius * sin(phi2) * sin(theta);
            glVertex3f(x2, y2, z2);
        }
        glEnd();
    }
}

void draw_minimap(const StatePacket& state, uint32_t self_id) {
    glPushAttrib(GL_VIEWPORT_BIT | GL_ENABLE_BIT);
    glMatrixMode(GL_PROJECTION); glPushMatrix();
    glMatrixMode(GL_MODELVIEW); glPushMatrix();
    glDisable(GL_DEPTH_TEST);
    int window_width, window_height;
    GLFWwindow* window = glfwGetCurrentContext();
    glfwGetWindowSize(window, &window_width, &window_height);
    int minimap_size = 200;
    glViewport(10, window_height - minimap_size - 10, minimap_size, minimap_size);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(0, MAP_WIDTH, 0, MAP_LENGTH, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glColor4f(0.1f, 0.1f, 0.1f, 0.7f);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBegin(GL_QUADS);
        glVertex2f(0, 0); glVertex2f(MAP_WIDTH, 0); glVertex2f(MAP_WIDTH, MAP_LENGTH); glVertex2f(0, MAP_LENGTH);
    glEnd();
    glDisable(GL_BLEND);
    glColor3f(1.0f, 1.0f, 1.0f);
    glBegin(GL_LINE_LOOP);
        glVertex2f(0, 0); glVertex2f(MAP_WIDTH, 0); glVertex2f(MAP_WIDTH, MAP_LENGTH); glVertex2f(0, MAP_LENGTH);
    glEnd();
    glColor3f(0.7f, 0.7f, 0.7f);
    glBegin(GL_QUADS);
    for (int x = 0; x < MAP_WIDTH; x++) {
        for (int y = 0; y < MAP_LENGTH; y++) {
            if (game_map[x][y] == WALL) {
                glVertex2f(x, y); glVertex2f(x + 1, y); glVertex2f(x + 1, y + 1); glVertex2f(x, y + 1);
            }
        }
    }
    glEnd();
    glPointSize(6.0f);
    glBegin(GL_POINTS);
    for (int i = 0; i < state.num_players; i++) {
        const PlayerState& p = state.players[i];
        if (!p.is_alive) continue;
        if (p.player_id == self_id) { glColor3f(0.0f, 1.0f, 1.0f); }
        else { glColor3f(1.0f, 0.0f, 0.0f); }
        glVertex2f(p.pos.x, p.pos.z);
    }
    glEnd();
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW); glPopMatrix();
    glPopAttrib();
}

void draw_crosshair() {
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();

    int window_width, window_height;
    GLFWwindow* window = glfwGetCurrentContext();
    glfwGetWindowSize(window, &window_width, &window_height);

    glOrtho(0.0, window_width, 0.0, window_height, -1.0, 1.0);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);

    float center_x = window_width / 2.0f;
    float center_y = window_height / 2.0f;
    float size = 10.0f;

    glColor4f(1.0f, 0.0f, 0.0f, 0.8f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glLineWidth(2.0f);

    glBegin(GL_LINES);
        glVertex2f(center_x - size, center_y);
        glVertex2f(center_x + size, center_y);

        glVertex2f(center_x, center_y - size);
        glVertex2f(center_x, center_y + size);
    glEnd();

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}

void renderGL(const StatePacket& state, uint32_t self_id, float playerX, float playerY, float playerZ) {
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.1f, 0.1f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    GLFWwindow* window = glfwGetCurrentContext();
    int width, height;
    glfwGetWindowSize(window, &width, &height);
    if (height == 0) height = 1;
    float aspect_ratio = (float)width / (float)height;
    glm::mat4 projection = glm::perspective(glm::radians(70.0f), aspect_ratio, 0.1f, 100.0f);
    glLoadMatrixf(glm::value_ptr(projection));
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();

    glm::vec3 playerPos = glm::vec3(playerX, playerY, playerZ);
    glm::vec3 playerEyePos = playerPos + glm::vec3(0.0f, 0.3f, 0.0f); // Camera offset
    glm::vec3 cameraFront;
    cameraFront.x = cos(glm::radians(cameraYaw)) * cos(glm::radians(cameraPitch));
    cameraFront.y = sin(glm::radians(cameraPitch));
    cameraFront.z = sin(glm::radians(cameraYaw)) * cos(glm::radians(cameraPitch));
    cameraFront = glm::normalize(cameraFront);
    glm::vec3 cameraTarget = playerEyePos + cameraFront;
    glm::mat4 view = glm::lookAt(playerEyePos, cameraTarget, glm::vec3(0.0f, 1.0f, 0.0f));
    glLoadMatrixf(glm::value_ptr(view));

    for (int x = 0; x < MAP_WIDTH; x++) {
        for (int y = 0; y < MAP_LENGTH; y++) {
            if (game_map[x][y] == WALL) { draw_cube((float)x, 0.0f, (float)y, 3.0f); }
        }
    }
    for (int i = 0; i < state.num_players; ++i) {
        const PlayerState& p = state.players[i];
        if (!p.is_alive || p.player_id == self_id) continue;
        glColor3f(0.0f, 1.0f, 0.0f);
        glPushMatrix();
        glTranslatef(p.pos.x, p.pos.y - 0.2f, p.pos.z);
        draw_sphere(PLAYER_RADIUS, 12, 12);
        glPopMatrix();
    }
    for (int i = 0; i < state.num_projectiles; ++i) {
        const ProjectileState& proj = state.projectiles[i];
        if (proj.is_active) {
            glColor3f(1.0f, 1.0f, 0.0f);
            glPushMatrix();
            glTranslatef(proj.pos.x, proj.pos.y, proj.pos.z);
            draw_sphere(PROJECTILE_RADIUS, 8, 8);
            glPopMatrix();
        }
    }
    draw_minimap(state, self_id);
    draw_crosshair();
}

MovementDirection get_movement_dir(GLFWwindow* window) {
    bool w = glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS;
    bool a = glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS;
    bool s = glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS;
    bool d = glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS;

    if (w && a) return FORWARD_LEFT;
    if (w && d) return FORWARD_RIGHT;
    if (s && a) return BACKWARDS_LEFT;
    if (s && d) return BACKWARDS_RIGHT;
    if (w) return FORWARD;
    if (a) return LEFT;
    if (s) return BACKWARDS;
    if (d) return RIGHT;
    
    return NONE;
}

void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (firstMouse) { lastMouseX = xpos; lastMouseY = ypos; firstMouse = false; }
    float xoffset = xpos - lastMouseX;
    float yoffset = lastMouseY - ypos;
    lastMouseX = xpos; lastMouseY = ypos;
    float sensitivity = 0.1f;
    xoffset *= sensitivity; yoffset *= sensitivity;
    cameraYaw += xoffset; cameraPitch += yoffset;
    if (cameraPitch > 89.0f) cameraPitch = 89.0f;
    if (cameraPitch < -89.0f) cameraPitch = -89.0f;
}

int main(int argc, char *argv[]) {
    if (argc < 3) { std::cerr << "Usage: " << argv[0] << " <server_ip> <port>\n"; return 1; }
    const char* server_ip = argv[1];
    int port = atoi(argv[2]);
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) { perror("socket"); return 1; }
    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
    if (!glfwInit()) { std::cerr << "Failed to initialize GLFW\n"; return -1; }
    GLFWwindow* window = glfwCreateWindow(1280, 720, "FPS Client", nullptr, nullptr);
    if (!window) { std::cerr << "Failed to create GLFW window\n"; glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetCursorPosCallback(window, mouse_callback);

    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    inet_aton(server_ip, &serv_addr.sin_addr);
    socklen_t serv_len = sizeof(serv_addr);
    uint32_t tick_id = 0;
    ProtoHeader join_pkt{};
    join_pkt.type = JOIN;
    join_pkt.tick_id = tick_id++;
    sendto(sockfd, &join_pkt, sizeof(join_pkt), 0, (sockaddr*)&serv_addr, serv_len);

    std::cout << "Waiting for server to assign ID...\n";
    JoinAckPacket ack_pkt{};
    fcntl(sockfd, F_SETFL, flags & ~O_NONBLOCK);
    recvfrom(sockfd, &ack_pkt, sizeof(ack_pkt), 0, nullptr, nullptr);
    MapPacket map_pkt;
    recvfrom(sockfd, &map_pkt, sizeof(map_pkt), 0, nullptr, nullptr);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

    uint32_t self_id = ack_pkt.your_id;
    if (map_pkt.hdr.type == MAP_DATA) {
        memcpy(game_map, map_pkt.map, sizeof(game_map));
    }
    std::cout << "Connected! My ID is: " << self_id << "\n";

    float posX = 1.0f, posY = 0.5f, posZ = 1.0f;
    bool am_i_alive = true;
    StatePacket last_valid_state{};

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) break;

        if (am_i_alive) {
            glm::vec3 view_dir_3d;
            view_dir_3d.x = cos(glm::radians(cameraYaw)) * cos(glm::radians(cameraPitch));
            view_dir_3d.y = sin(glm::radians(cameraPitch));
            view_dir_3d.z = sin(glm::radians(cameraYaw)) * cos(glm::radians(cameraPitch));
            view_dir_3d = glm::normalize(view_dir_3d);

            ActionPacket pkt{};
            pkt.hdr.type = ACT;
            pkt.hdr.tick_id = tick_id++;
            pkt.view_dir = view_dir_3d;
            pkt.movement_dir = get_movement_dir(window);
            
            bool isFiring = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            auto now = Clock::now();
            if (isFiring && std::chrono::duration_cast<std::chrono::milliseconds>(now - last_fire_time).count() >= 200) {
                pkt.is_firing = true;
                last_fire_time = now;
            } else {
                pkt.is_firing = false;
            }
            sendto(sockfd, &pkt, sizeof(pkt), 0, (sockaddr*)&serv_addr, serv_len);
        }

        StatePacket received_state{};
        ssize_t len = recvfrom(sockfd, &received_state, sizeof(received_state), 0, nullptr, nullptr);
        if (len >= (ssize_t)sizeof(ProtoHeader)) {
            last_valid_state = received_state;
        }

        for (int i = 0; i < last_valid_state.num_players; ++i) {
            if (last_valid_state.players[i].player_id == self_id) {
                posX = last_valid_state.players[i].pos.x;
                posY = last_valid_state.players[i].pos.y;
                posZ = last_valid_state.players[i].pos.z;
                am_i_alive = last_valid_state.players[i].is_alive;
                break;
            }
        }
        
        renderGL(last_valid_state, self_id, posX, posY, posZ);
        glfwSwapBuffers(window);
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    close(sockfd);
    return 0;
}
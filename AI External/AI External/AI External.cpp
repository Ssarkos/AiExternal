
#include "hazedumper.hpp" // Include the Hazedumper header file

#include <Windows.h> // Include the Windows API
#include <iostream> // Include the standard input/output stream library
#include <tlhelp32.h> // Include the tlhelp32 header file


template<typename T>
T RPM(HANDLE hProcess, LPVOID lpBaseAddress)
{
    T data;
    SIZE_T nBytesRead;
    if (ReadProcessMemory(hProcess, lpBaseAddress, &data, sizeof(data), &nBytesRead))
    {
        return data;
    }
    else
    {
        // Read failed
        return T();
    }
}

template<typename T>
bool WPM(HANDLE hProcess, LPVOID lpBaseAddress, T data)
{
    SIZE_T nBytesWritten;
    return WriteProcessMemory(hProcess, lpBaseAddress, &data, sizeof(data), &nBytesWritten);
}

using namespace std; // Use the standard namespace

// Declare the GetProcessId and GetModuleBaseAddress functions
DWORD GetProcessId(const char* processName);
DWORD GetModuleBaseAddress(DWORD processId, const char* moduleName);

// Define the GetProcessId function that returns the process ID of a specified process
DWORD GetProcessId(const char* processName) {
    // Declare the variables that will be used in the function
    HANDLE snapshot;
    PROCESSENTRY32 entry;

    // Take a snapshot of the system's processes
    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;

    // Set the size of the PROCESSENTRY32 struct
    entry.dwSize = sizeof(entry);

    // Get the first process in the snapshot
    if (!Process32First(snapshot, &entry)) {
        CloseHandle(snapshot);
        return 0;
    }

    // Iterate over the processes in the snapshot
    // and return the process ID of the specified process
    do {
        if (!_stricmp(entry.szExeFile, processName)) {
            CloseHandle(snapshot);
            return entry.th32ProcessID;
        }
    } while (Process32Next(snapshot, &entry));

    // Close the snapshot handle and return 0 if the process was not found
    CloseHandle(snapshot);
    return 0;
}

// Define the GetModuleBaseAddress function that returns the base address of a specified module in a specified process
DWORD GetModuleBaseAddress(DWORD processId, const char* moduleName) {
    // Declare the variables that will be used in the function
    HANDLE snapshot;
    MODULEENTRY32 entry;

    // Take a snapshot of the specified process's modules
    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, processId);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;

    // Set the size of the MODULEENTRY32 struct
    entry.dwSize = sizeof(entry);

    // Get the first module in the snapshot
    if (!Module32First(snapshot, &entry)) {
        CloseHandle(snapshot);
        return 0;
    }

    // Iterate over the modules in the snapshot
    // and return the base address of the specified module
    do {
        if (!_stricmp(entry.szModule, moduleName)) {
            CloseHandle(snapshot);
            return (DWORD)entry.modBaseAddr;
        }
    } while (Module32Next(snapshot, &entry));

    // Close the snapshot handle and return 0 if the module was not found
    CloseHandle(snapshot);
    return 0;
}

// Define the Vector3D class that will be used to store 3D vectors
class Vector3D {
public:
    float x, y, z;

    // Implement the - operator that subtracts two Vector3D objects
    Vector3D operator-(const Vector3D& other) {
        Vector3D result;
        result.x = x - other.x;
        result.y = y - other.y;
        result.z = z - other.z;
        return result;
    }

    // Implement the + operator that adds two Vector3D objects
    Vector3D operator+(const Vector3D& other) {
        Vector3D result;
        result.x = x + other.x;
        result.y = y + other.y;
        result.z = z + other.z;
        return result;
    }

    bool operator!=(const Vector3D& other) {
        if (x != other.x && y != other.y && z != other.z) {
            return true;
        }
        return false;
    }
};

// Declare the global variables that will be used in the program
// These include the handle to the game process, the base address of the game module,
// the process ID and module name of the game, and the target position for the aimbot
HANDLE gameHandle;
DWORD baseAddress;
DWORD dwEngine;
DWORD gamePID;
const char* exeName = "csgo.exe";
const char* gameModule = "client.dll";
const char* engineModule = "engine.dll";
Vector3D targetPosition;

// Define the CalculateAngle function that calculates the angle between two 3D vectors
Vector3D CalculateAngle(Vector3D playerPosition, Vector3D targetPosition) {
    Vector3D aimAngle;
    Vector3D delta = playerPosition - targetPosition;

    float hyp = sqrtf(delta.x * delta.x + delta.y * delta.y);

    aimAngle.x = atanf(delta.z / hyp) * (180.0f / 3.14159265358979323846f);
    aimAngle.y = atanf(delta.y / delta.x) * (180.0f / 3.14159265358979323846f);
    aimAngle.z = 0.0f;

    if (delta.x >= 0.0f)
        aimAngle.y += 180.0f;

    return aimAngle;
}

Vector3D SmoothAngle(Vector3D currentAngle, Vector3D aimAngle, float smoothing) {
    Vector3D smoothedAngle;

    smoothedAngle.x = aimAngle.x - currentAngle.x;
    smoothedAngle.y = aimAngle.y - currentAngle.y;
    smoothedAngle.z = 0.0f;

    smoothedAngle.x = currentAngle.x + smoothedAngle.x / smoothing;
    smoothedAngle.y = currentAngle.y + smoothedAngle.y / smoothing;
    smoothedAngle.z = 0.0f;

    return smoothedAngle;
}

// Declare the CalculateDistance function that calculates the distance between two 3D points
float CalculateDistance(Vector3D point1, Vector3D point2) {
    // Calculate the differences in the x, y, and z components between the two points
    float dx = point2.x - point1.x;
    float dy = point2.y - point1.y;
    float dz = point2.z - point1.z;

    // Use the distance formula to calculate the distance between the two points
    float distance = sqrt(dx * dx + dy * dy + dz * dz);

    // Return the calculated distance
    return distance;
}

// Declare the FindClosestPlayer function that returns the ID of the closest player from the entity list
int FindClosestPlayer(HANDLE gameHandle, DWORD baseAddress, Vector3D playerPosition) {
    // Declare the variables that will be used in the function
    Vector3D entityPosition;
    float closestDistance = FLT_MAX; // Set the closest distance to the maximum float value
    int closestEntity = -1; // Set the closest entity to -1 (invalid entity)
    int entityHealth;

    // Iterate over the entities in the game
    for (int i = 0; i < 64; i++) {
        // Read the position of the current entity from the game's memory
        entityPosition = RPM<Vector3D>(gameHandle, (BYTE*)RPM<DWORD>(gameHandle, (BYTE*)baseAddress + hazedumper::signatures::dwEntityList + (i * 0x10)) + hazedumper::netvars::m_vecOrigin);
        entityPosition = entityPosition + RPM<Vector3D>(gameHandle, (BYTE*)RPM<DWORD>(gameHandle, (BYTE*)baseAddress + hazedumper::signatures::dwEntityList + (i * 0x10)) + hazedumper::netvars::m_vecViewOffset);

        entityHealth = RPM<int>(gameHandle, (BYTE*)RPM<DWORD>(gameHandle, (BYTE*)baseAddress + hazedumper::signatures::dwEntityList + (i * 0x10)) + hazedumper::netvars::m_iHealth);

        // Calculate the distance between the player and the current entity
        float distance = CalculateDistance(playerPosition, entityPosition);

        // If the current entity is closer to the player than the previously closest entity,
        // set the closest entity to the current entity and update the closest distance
        if (distance < closestDistance && distance > 0 && entityHealth > 0 && entityHealth < 101) { //&& entityPosition.x != playerPosition.x) {
            closestDistance = distance;
            closestEntity = i;
        }
    }

    // Return the ID of the closest entity (player)
    return closestEntity;
}

int main() {
    // Set the target position of the aimbot
    targetPosition.x = 0.0f;
    targetPosition.y = 0.0f;
    targetPosition.z = 0.0f;

    // Set the smoothing value for the aimbot
    float smoothing = 1.0f;

    // Get the process ID and base address of the game
    gamePID = GetProcessId(exeName);
    baseAddress = GetModuleBaseAddress(gamePID, gameModule);
    dwEngine = GetModuleBaseAddress(gamePID, engineModule);

    // Open a handle to the game process with read/write access
    gameHandle = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION, 0, gamePID);

    // Check if the handle was successfully opened
    if (!gameHandle) {
        cout << "Failed to open handle to game process!" << endl;
        return -1;
    }

    // Declare the variables that will be used in the main loop
    // These include the player's position, view angle, aim angle, and enemy player's position
    Vector3D playerPosition, viewAngle, aimAngle, enemyPosition;

    // Main loop that runs the aimbot
    while (true) {
       
        // Read the player's position from the game's memory
        playerPosition = RPM<Vector3D>(gameHandle, (BYTE*)RPM<DWORD>(gameHandle, (BYTE*)baseAddress + hazedumper::signatures::dwLocalPlayer) + hazedumper::netvars::m_vecOrigin);
        playerPosition = playerPosition + RPM<Vector3D>(gameHandle, (BYTE*)RPM<DWORD>(gameHandle, (BYTE*)baseAddress + hazedumper::signatures::dwLocalPlayer) + hazedumper::netvars::m_vecViewOffset);

        // Find the closest player from the entity list and store its ID in the targetId variable
        int targetId = FindClosestPlayer(gameHandle, baseAddress, playerPosition);

        if (targetId == -1)
            continue;

        // Read the position of the enemy player from the game's memory
        enemyPosition = RPM<Vector3D>(gameHandle, (BYTE*)RPM<DWORD>(gameHandle, (BYTE*)baseAddress + hazedumper::signatures::dwEntityList + (targetId * 0x10)) + hazedumper::netvars::m_vecOrigin);
        enemyPosition = enemyPosition + RPM<Vector3D>(gameHandle, (BYTE*)RPM<DWORD>(gameHandle, (BYTE*)baseAddress + hazedumper::signatures::dwEntityList + (targetId * 0x10)) + hazedumper::netvars::m_vecViewOffset);

        // Set the target position to the position of the enemy player
        targetPosition = enemyPosition; 

        // Calculate the aim angle based on the player's position and the target position
        aimAngle = CalculateAngle(playerPosition, targetPosition);

        // Smooth out the aim angle using the specified smoothing value
        viewAngle = RPM<Vector3D>(gameHandle, (BYTE*)RPM<DWORD>(gameHandle, (BYTE*)dwEngine + hazedumper::signatures::dwClientState) + hazedumper::signatures::dwClientState_ViewAngles);
        aimAngle = SmoothAngle(viewAngle, aimAngle, smoothing);

        // Write the aim angle back to the game's memory
        WPM<Vector3D>(gameHandle, (BYTE*)RPM<DWORD>(gameHandle, (BYTE*)dwEngine + hazedumper::signatures::dwClientState) + hazedumper::signatures::dwClientState_ViewAngles, aimAngle);

        // Sleep for 1 ms to avoid using too much CPU
        Sleep(1);
    }

    // Close the handle to the game process
    CloseHandle(gameHandle);

    return 0;
}

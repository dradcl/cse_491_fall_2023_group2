/**
 * This file is part of the Fall 2023, CSE 491 course project.
 * @brief A client-side networking main to prototype communication
 * @note Status: PROPOSAL
 **/

// // Include the modules that we will be using.
#include "../../TrashInterface.hpp"
#include "../../../Agents/PacingAgent.hpp"
#include "../../../Worlds/MazeWorld.hpp"
#include "networkingagent.hpp"
#include "ClientInterface.hpp"

int main(int argc, char *argv[])
{
  if (argc != 3) {
    std::cout << "Must have an argument for server IP and port\nUsage: ./client [IP] [port]" << std::endl;
    return 1;
  }

  std::string ip_string(argv[1]);
  unsigned short port = stoi(std::string(argv[2]));
  cse491::netWorth::ClientInterface interface(ip_string, port);

  if (!interface.EstablishConnection()) return 1;
  interface.Loop();

  return 0;
}

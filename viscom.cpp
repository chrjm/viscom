#define OLC_PGE_APPLICATION
#define OLC_PGEX_PANZOOM

#include <fstream>
#include <iomanip>
#include <vector>

#include "olcPixelGameEngine.h"
#include "olcPGEX_PanZoom.h"

struct Terminal
{
	int id;
	olc::vi2d pos;
	bool state = FALSE;
	char type; // S = source start, Z = source end, L = clock, U = buffer,, 
	           // transistor: C = collector, B = base, E = emitter, N = not out
	           // gated latch: D = data in, W = write enable, Q = data out
	           

	int componentId;
};

struct Component
{
	int id;
	std::string type;
	olc::vi2d pos;
};

struct Connection
{
	int id;
	int terminalA;
	int terminalB;
	olc::vi2d terminalAPos;
	olc::vi2d terminalBPos;
	int notOutTerminal;
	bool state = FALSE;
};


class Viscom : public olc::PixelGameEngine
{
public:
	Viscom()
	{
		sAppName = "viscom";
	}

public:
	bool OnUserCreate() override
	{
		pz.Create(this);
		pz.SetOffset({ (float)-ScreenWidth() * 0.5f, (float)-ScreenHeight() * 0.5f });

		if (!componentBuilderMode)
		{
			terminals.push_back({ 1, { 25, 0 }, TRUE, 'S', 1 });
			terminals.push_back({ 2, { -25, 0 }, FALSE, 'Z', 1 });
		}

		terminals.push_back({ 3, { -200, 0 }, FALSE, 'L', 1 });
		
		return true;
	}

	bool OnUserUpdate(float fElapsedTime) override
	{
		if (GetKey(olc::P).bReleased)
		{
			if (!simulationPaused)
				simulationPaused = TRUE;
			else
				simulationPaused = FALSE;

			redrawRequired = TRUE;
		}

		simulateClock();

		if (GetKey(olc::Key::UP).bReleased)
		{
			clockSpeed++;
			redrawRequired = TRUE;
		}

		if (GetKey(olc::Key::DOWN).bReleased)
		{
			if (clockSpeed > 0)
			{
				clockSpeed--;
				redrawRequired = TRUE;
			}
		}


		if (GetMouseWheel() > 0)
		{
			pz.ZoomIn(1.1f);
			redrawRequired = TRUE;
		}
			
		if (GetMouseWheel() < 0)
		{
			pz.ZoomOut(0.9f);
			redrawRequired = TRUE;
		}
			
		if (GetKey(olc::Z).bReleased)
		{
			pz.SetScale({ 1.0f, 1.0f });
			redrawRequired = TRUE;
		}
			
		// Pan
		if (GetMouse(2).bHeld)
			redrawRequired = TRUE;

		if (GetMouse(2).bPressed)
			pz.StartPan();
			
		if (GetMouse(2).bReleased)
			pz.StopPan();

		// Draw mouse guides
		if (GetKey(olc::X).bHeld || GetKey(olc::X).bReleased)
		{
			redrawRequired = TRUE;
		}
		

		pz.Update(fElapsedTime);

		/*if (GetKey(olc::Key::R).bReleased)
		{
			components.clear();
			connections.clear();
			sourceConnections.clear();
			terminals.clear();
			lastTerminalId = 3;
			lastConnectionId = 1;
			lastComponentId = 2;
			selectedTerminalA = 0;
			selectedTerminalB = 0;
			selectedTerminalAPos = { 0, 0 };
			selectedTerminalBPos = { 0, 0 };
			activeInventoryComponent = 0;
			updateSimulation = FALSE;

			terminals.push_back({ 1, olc::vi2d(500 + 25, 50), TRUE, 'S', 1 });
			terminals.push_back({ 2, olc::vi2d(500 - 25, 50), FALSE, 'Z', 1 });
		}*/
		
		// --------------------
		// Run Logic Simulation

		if (updateSimulation && !simulationPaused)
		{
			redrawRequired = TRUE;

			for (auto &connection : connections)
				connection.state = FALSE;

			for (auto &terminal : terminals)
				if (terminal.type != 'S' && terminal.type != 'Q')
				{
					terminal.state = FALSE;
				}

			for (auto& terminal : terminals)
			{
				if (terminal.type == 'L')
					terminal.state = clockState;
			}				

			std::vector<int> transistorsToSimulate;
			std::vector<int> gatedLatchesToSimulate;
			std::vector<Terminal*> activeTerminals;
			std::vector<Connection*> activeConnections;

			for (auto currentConnection : sourceConnections)
			{
				if (currentConnection->terminalA == 1)
					currentConnection->state = TRUE;

				if (currentConnection->terminalA == 3)
					currentConnection->state = clockState;

				Terminal* currentTerminalB = findTerminal(currentConnection->terminalB);

				if (currentTerminalB)
				{
					currentTerminalB->state = currentConnection->state;

					if (currentTerminalB->type == 'U')
					{
						activeTerminals.push_back(currentTerminalB);
					}
					else if (currentTerminalB->type == 'C' || currentTerminalB->type == 'E' || currentTerminalB->type == 'B')
					{
						transistorsToSimulate.push_back(currentTerminalB->componentId);
					}
					else if (currentTerminalB->type == 'D' || currentTerminalB->type == 'W' || currentTerminalB->type == 'Q')
					{
						gatedLatchesToSimulate.push_back(currentTerminalB->componentId);
					}
						
				}
			}

			bool forceLeave = FALSE;
			std::vector<int> visitedTerminals;
			int highestFound = 0;

			while ((transistorsToSimulate.size() || gatedLatchesToSimulate.size() || activeTerminals.size()) && !forceLeave)
			{
				for (auto transistorId : transistorsToSimulate)
				{
					Terminal* thisNotOut = findTerminalByComponent(transistorId, 'N');
					Terminal* thisEmitter = findTerminalByComponent(transistorId, 'E');

					if (thisNotOut)
					{
						thisNotOut->state = FALSE;
						activeTerminals.push_back(thisNotOut);
					}

					if (thisEmitter)
					{
						thisEmitter->state = FALSE;
						activeTerminals.push_back(thisEmitter);
					}

					Terminal* thisNextTerminal = simulateTransistor(transistorId);

					if (thisNextTerminal)
					{
						thisNextTerminal->state = TRUE;
						activeTerminals.push_back(thisNextTerminal);
					}
				}

				transistorsToSimulate.clear();

				for (auto gatedLatchId : gatedLatchesToSimulate)
				{
					Terminal* thisDataOut = findTerminalByComponent(gatedLatchId, 'Q');

					bool originalDataOutState = FALSE;
					if (thisDataOut->state)
						originalDataOutState = TRUE;

					simulateGatedLatch(gatedLatchId);

					if (originalDataOutState != thisDataOut->state)
					{
						activeTerminals.push_back(thisDataOut);
					}
				}

				gatedLatchesToSimulate.clear();

				for (auto terminal : activeTerminals)
				{

					// std::cout << "Visiting terminal with id: " << terminal->id << std::endl;

					int alreadyVisitedCount = 0;
					for (auto visitedTerminalId : visitedTerminals)
					{
						if (visitedTerminalId == terminal->id)
							alreadyVisitedCount += 1;
					}


					/*if (alreadyVisitedCount > 0)
						std::cout << "Already visited! This many times: " << alreadyVisitedCount << std::endl;
					else
						print("Not already visited...");*/

					visitedTerminals.push_back(terminal->id);

					if (alreadyVisitedCount > highestFound)
						highestFound = alreadyVisitedCount;

					if (alreadyVisitedCount > 8000)
					{
						forceLeave = TRUE;
					}

					for (auto& connection : connections)
					{
						if (connection.terminalA == terminal->id)
						{
							connection.state = terminal->state;
							activeConnections.push_back(&connection);
						}
					}
				}

				activeTerminals.clear();

				for (auto connection : activeConnections)
				{
					Terminal* currentTerminalB = findTerminal(connection->terminalB);

					if (currentTerminalB)
					{
						currentTerminalB->state = connection->state;

						if (currentTerminalB->type == 'U')
							activeTerminals.push_back(currentTerminalB);
						else if (currentTerminalB->type == 'B' || currentTerminalB->type == 'C' || currentTerminalB->type == 'E')
							transistorsToSimulate.push_back(currentTerminalB->componentId);
						else if (currentTerminalB->type == 'D' || currentTerminalB->type == 'W' || currentTerminalB->type == 'Q')
							gatedLatchesToSimulate.push_back(currentTerminalB->componentId);
					}
				}

				activeConnections.clear();

				for (auto connection : connections)
				{
					if (connection.state)
					{
						for (auto& terminal : terminals)
						{
							if (terminal.id == connection.terminalB)
							{
								terminal.state = TRUE;
							}
						}
					}
				}
			}

			updateSimulation = FALSE;

			std::cout << "Highest terminal visit count: " << highestFound << std::endl;
		}

		//---------------------
		
		if (GetMouse(0).bReleased)
		{
			if (!placingModule)
			{
				components.push_back({ lastComponentId, inventoryComponents[activeInventoryComponent], GetWorldMouse() });

				if (inventoryComponents[activeInventoryComponent] == "TRANSISTOR")
				{
					terminals.push_back({ lastTerminalId, GetWorldMouse() + olc::vi2d(25, -25), FALSE, 'C', lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, GetWorldMouse() + olc::vi2d(-25, 0), FALSE, 'B', lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, GetWorldMouse() + olc::vi2d(25, 25), FALSE, 'E', lastComponentId });
					lastTerminalId++;
				}

				if (inventoryComponents[activeInventoryComponent] == "GATED LATCH")
				{
					terminals.push_back({ lastTerminalId, GetWorldMouse() + olc::vi2d(-25, -25), FALSE, 'D', lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, GetWorldMouse() + olc::vi2d(-25, 25), FALSE, 'W', lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, GetWorldMouse() + olc::vi2d(25, 0), FALSE, 'Q', lastComponentId });
					lastTerminalId++;
				}

				if (inventoryComponents[activeInventoryComponent] == "BUFFER")
				{
					terminals.push_back({ lastTerminalId, GetWorldMouse(), FALSE, 'U', lastComponentId });
					lastTerminalId++;
				}

				if (inventoryComponents[activeInventoryComponent] == "LED")
				{
					terminals.push_back({ lastTerminalId, GetWorldMouse(), FALSE, 'U', lastComponentId });
					lastTerminalId++;
				}

				lastComponentId++;
			}
			else
			{
				PlaceModule(inventoryModules[activeInventoryModule]);
			}
			

			redrawRequired = TRUE;
		}

		if (GetKey(olc::DEL).bReleased)
		{
			deleteClosest();
			redrawRequired = TRUE;
		}
			

		if (GetKey(olc::S).bReleased)
			Save();

		if (GetKey(olc::L).bReleased)
		{
			Save();
			Load();
			redrawRequired = TRUE;
		}

		if (GetMouse(1).bReleased)
		{
			bool needsNotOut = FALSE;
			int transistorId = 0;

			if (!selectedTerminalA)
			{
				double smallestDistance = 0.00;
				int closestTerminalId = 0;
				olc::vi2d closestTerminalPos;

				for (auto terminal : terminals)
				{
					double distance = CalculateDistance(terminal.pos, GetWorldMouse());

					if (distance < smallestDistance || smallestDistance == 0.00)
					{
						if (terminal.type == 'S' || terminal.type == 'E' || terminal.type == 'N' || terminal.type == 'U' || terminal.type == 'Q' || terminal.type == 'L')
						{
							smallestDistance = distance;
							closestTerminalId = terminal.id;
							closestTerminalPos = terminal.pos;
						}
					}
				}

				if (smallestDistance < 10.00)
				{
					selectedTerminalA = closestTerminalId;
					selectedTerminalAPos = closestTerminalPos;
				}				
			}
			else
			{
				double smallestDistance = 0.00;
				int closestTerminalId = 0;
				olc::vi2d closestTerminalPos;

				for (auto terminal : terminals)
				{
					double distance = CalculateDistance(terminal.pos, GetWorldMouse());

					if (distance < smallestDistance || smallestDistance == 0.00)
					{
						if (terminal.type == 'C' || terminal.type == 'B' || terminal.type == 'Z' || terminal.type == 'U' || terminal.type == 'D' || terminal.type == 'W')
						{
							smallestDistance = distance;
							closestTerminalId = terminal.id;
							closestTerminalPos = terminal.pos;
						}

						if (terminal.type == 'C')
						{
							needsNotOut = TRUE;
							transistorId = terminal.componentId;
						}
						else
						{
							needsNotOut = FALSE;
							transistorId = 0;
						}
					}
				}


				if (smallestDistance < 10.00)
				{
					selectedTerminalB = closestTerminalId;
					selectedTerminalBPos = closestTerminalPos;
				}
				
			}


			if (selectedTerminalA && selectedTerminalB)
			{
				int notOutTerminalId = 0;

				if (needsNotOut)
					notOutTerminalId = lastTerminalId;

				Connection newConnection = { 
					lastConnectionId,
					selectedTerminalA,
					selectedTerminalB,
					selectedTerminalAPos,
					selectedTerminalBPos,
					notOutTerminalId,
				};
				lastConnectionId++;
				connections.push_back(newConnection);
			
				if (needsNotOut)
				{
					olc::vi2d notOutPos = calculateNotOut(selectedTerminalAPos, selectedTerminalBPos);
					terminals.push_back({ lastTerminalId, notOutPos, FALSE, 'N', transistorId });
					lastTerminalId++;
				}

				updateSourceConnections();
				updateSimulation = TRUE;

				selectedTerminalA = 0;
				selectedTerminalB = 0;
				selectedTerminalAPos = olc::vi2d(0, 0);
				selectedTerminalBPos = olc::vi2d(0, 0);
			}

			redrawRequired = TRUE;
		}
		
		if (GetKey(olc::Key::SPACE).bReleased)
		{
			if (activeInventoryComponent < (int) inventoryComponents.size() - 1)
				activeInventoryComponent++;
			else
				activeInventoryComponent = 0;

			redrawRequired = TRUE;
		}

		if (GetKey(olc::Key::N).bReleased)
		{
			if (activeInventoryModule < (int)inventoryModules.size() - 1)
				activeInventoryModule++;
			else
				activeInventoryModule = 0;

			redrawRequired = TRUE;
		}

		if (GetKey(olc::Key::M).bReleased)
		{
			if (!placingModule)
				placingModule = TRUE;
			else
				placingModule = FALSE;

			redrawRequired = TRUE;
		}

		if (placingModule)
			redrawRequired = TRUE;

		olc::vi2d sourceScreenPosition;
		pz.WorldToScreen({ 0, 0 }, sourceScreenPosition);

		olc::vi2d clockScreenPosition;
		pz.WorldToScreen({ -200, 0 }, clockScreenPosition);

		if (redrawRequired)
		{
			Clear(olc::BLACK);

			if (!componentBuilderMode)
				DrawSource(sourceScreenPosition);
			else
				DrawCircle(sourceScreenPosition, 2, olc::DARK_GREY);

			DrawClock(clockScreenPosition);

			if (GetKey(olc::X).bHeld)
			{
				DrawLine({ GetMouseX(), 0 }, { GetMouseX(), ScreenHeight() }, { 64, 64, 64 }, 0xF0F0F0F0);
				DrawLine({ 0, GetMouseY() }, { ScreenWidth(), GetMouseY() }, { 64, 64, 64 }, 0xF0F0F0F0);
			}
			
			DrawComponents();
			DrawConnections();
			DrawTerminals();

			if (placingModule)
			{
				std::vector<olc::vi2d> ghostCoordinates = moduleCoordinates[activeInventoryModule];
				std::vector<olc::vi2d> ghostCoordinatesScreen(ghostCoordinates.size());

				for (int i = 0; i < ghostCoordinates.size(); i++)
					pz.WorldToScreen(ghostCoordinates[i] + GetWorldMouse(), ghostCoordinatesScreen[i]);

				if (ghostCoordinatesScreen.size() >= 4)
				{
					DrawLine(ghostCoordinatesScreen[0], ghostCoordinatesScreen[1], olc::GREY, 0xF0F0F0F0);
					DrawLine(ghostCoordinatesScreen[0], ghostCoordinatesScreen[2], olc::GREY, 0xF0F0F0F0);
					DrawLine(ghostCoordinatesScreen[1], ghostCoordinatesScreen[3], olc::GREY, 0xF0F0F0F0);
					DrawLine(ghostCoordinatesScreen[2], ghostCoordinatesScreen[3], olc::GREY, 0xF0F0F0F0);
				}
			}

			DrawStrings();
			redrawRequired = FALSE;
		}
		
		return !(GetKey(olc::ESCAPE).bPressed);
	}

private:
	olc::panzoom pz;
	bool clockState = FALSE;
	int clockSpeed = 0;
	int clockTicks = 0;
	std::vector<Component> components;
	std::vector<Connection> connections;
	std::vector<Connection*> sourceConnections;
	std::vector<Terminal> terminals;
	int lastTerminalId = 4;
	int lastConnectionId = 1;
	int lastComponentId = 2;
	int selectedTerminalA = 0;
	int selectedTerminalB = 0;
	olc::vi2d selectedTerminalAPos;
	olc::vi2d selectedTerminalBPos;
	int activeInventoryComponent = 0;
	std::vector<std::string> inventoryComponents = {
		"GATED LATCH",
		"TRANSISTOR",
		"BUFFER",
		"LED",
	};
	int activeInventoryModule = 0;
	bool placingModule = FALSE;
	std::vector<std::vector<olc::vi2d>> moduleCoordinates = {
		{{-53, -88}, {60, -88}, {-53, 76}, {60, 76}},         // AND
		{{-101, -48}, {97, -48}, {-101, 51}, {97, 51}},       // OR
		{{-53, -90}, {158, -90}, {-53, 76}, {158, 76}},       // NAND
		{{-151, -130}, {241, -130}, {-151, 191}, {241, 191}}, // XOR
		{{-481, -265}, {468, -265}, {-481, 302}, {468, 302}}, // ADDER
		{{-355, -187}, {354, -187}, {-355, 177}, {354, 177}}, // REG
		{{-339, 103}, {800, 103}, {-339, 520}, {800, 520}},   // REGBUS
		{{-377, 103}, {800, 103}, {-377, 932}, {800, 932}}    // REGS
	};
	std::vector<std::string> inventoryModules = {
		"AND",
		"OR",
		"NAND",
		"XOR",
		"ADDER",
		"REG",
		"REGBUS",
		"REGS",
	};
	bool simulationPaused = FALSE;
	bool updateSimulation = FALSE;
	bool componentBuilderMode = FALSE;
	bool redrawRequired = TRUE;

	void Save()
	{
		std::string timestamp = std::to_string(std::time(0));
		std::string filepath = "saves/" + timestamp + "_";

		std::string globalsFilepath = filepath + "globals.txt";
		std::ofstream globalsFile(globalsFilepath);
		globalsFile << lastTerminalId << std::endl;
		globalsFile << lastConnectionId << std::endl;
		globalsFile << lastComponentId << std::endl;

		
		std::string componentsFilepath = filepath + "components.txt";
		std::ofstream componentsFile(componentsFilepath);

		for (auto component : components)
		{
			componentsFile << component.id << "," << component.type << ",";
			componentsFile << component.pos.x << "," << component.pos.y << std::endl;
		}

		std::string connectionsFilepath = filepath + "connections.txt";
		std::ofstream connectionsFile(connectionsFilepath);

		for (auto connection : connections)
		{
			connectionsFile << connection.id << "," << connection.terminalA << ",";
			connectionsFile << connection.terminalB << ",";
			connectionsFile << connection.terminalAPos.x << "," << connection.terminalAPos.y << ",";
			connectionsFile << connection.terminalBPos.x << "," << connection.terminalBPos.y << ",";
			connectionsFile << connection.notOutTerminal << std::endl;
		}

		std::string terminalsFilepath = filepath + "terminals.txt";
		std::ofstream terminalsFile(terminalsFilepath);

		for (auto terminal : terminals)
		{
			terminalsFile << terminal.id << "," << terminal.pos.x << "," << terminal.pos.y << ",";
			terminalsFile << terminal.type << "," << terminal.componentId << std::endl;
		}
	}

	void PlaceModule(std::string module_name)
	{
		std::string filepath = "modules/" + module_name + "_";

		std::ifstream componentsFile(filepath + "components.txt");
		std::string rawId;
		std::string rawType;
		std::string rawPosX;
		std::string rawPosY;
		int lastComponentIdOffset = 0;

		while (std::getline(componentsFile, rawId, ','))
		{
			std::getline(componentsFile, rawType, ',');
			std::getline(componentsFile, rawPosX, ',');
			std::getline(componentsFile, rawPosY, '\n');

			components.push_back({ stoi(rawId) + lastComponentId, rawType, olc::vi2d(stoi(rawPosX), stoi(rawPosY)) + GetWorldMouse() });
			lastComponentIdOffset = stoi(rawId);
		}

		std::ifstream connectionsFile(filepath + "connections.txt");
		std::string rawConnectionId;
		std::string rawTerminalA;
		std::string rawTerminalB;
		std::string rawTerminalAPosX;
		std::string rawTerminalAPosY;
		std::string rawTerminalBPosX;
		std::string rawTerminalBPosY;
		std::string rawNotOutTerminal;
		int lastConnectionIdOffset = 0;

		while (std::getline(connectionsFile, rawConnectionId, ','))
		{
			std::getline(connectionsFile, rawTerminalA, ',');
			std::getline(connectionsFile, rawTerminalB, ',');
			std::getline(connectionsFile, rawTerminalAPosX, ',');
			std::getline(connectionsFile, rawTerminalAPosY, ',');
			std::getline(connectionsFile, rawTerminalBPosX, ',');
			std::getline(connectionsFile, rawTerminalBPosY, ',');
			std::getline(connectionsFile, rawNotOutTerminal, '\n');

			connections.push_back({
				stoi(rawConnectionId) + lastConnectionId,
				stoi(rawTerminalA) + lastTerminalId,
				stoi(rawTerminalB) + lastTerminalId,
				olc::vi2d(stoi(rawTerminalAPosX), stoi(rawTerminalAPosY)) + GetWorldMouse(),
				olc::vi2d(stoi(rawTerminalBPosX), stoi(rawTerminalBPosY)) + GetWorldMouse(),
				stoi(rawNotOutTerminal) + lastTerminalId
				});

			lastConnectionIdOffset = stoi(rawConnectionId);
		}

		std::ifstream terminalsFile(filepath + "terminals.txt");
		std::string rawTerminalId;
		std::string rawTerminalPosX;
		std::string rawTerminalPosY;
		std::string rawTerminalType;
		std::string rawTerminalComponentId;
		int lastTerminalIdOffset = 0;

		while (std::getline(terminalsFile, rawTerminalId, ','))
		{
			std::getline(terminalsFile, rawTerminalPosX, ',');
			std::getline(terminalsFile, rawTerminalPosY, ',');
			std::getline(terminalsFile, rawTerminalType, ',');
			std::getline(terminalsFile, rawTerminalComponentId, '\n');

			terminals.push_back({
				stoi(rawTerminalId) + lastTerminalId,
				olc::vi2d(stoi(rawTerminalPosX), stoi(rawTerminalPosY)) + GetWorldMouse(),
				FALSE,
				rawTerminalType[0],
				stoi(rawTerminalComponentId) + lastComponentId,
				});

			lastTerminalIdOffset = stoi(rawTerminalId);
		}


		lastComponentId += lastComponentIdOffset + 1;
		lastConnectionId += lastConnectionIdOffset + 1;
		lastTerminalId += lastTerminalIdOffset + 1;

	}

	void Load()
	{
		std::string load_timestamp = "1627806576";
		std::string filepath = "saves/" + load_timestamp + "_";
		std::ifstream globalsFile(filepath + "globals.txt");

		int currentLine = 0;
		if (globalsFile.is_open())
		{
			std::string line;

			while (std::getline(globalsFile, line))
			{
				if (currentLine == 0)
				{
					lastTerminalId = std::stoi(line);
				}
				if (currentLine == 1)
				{
					lastConnectionId = std::stoi(line);
				}
				if (currentLine == 2)
				{
					lastComponentId = std::stoi(line);
				}

				currentLine++;
			}

			globalsFile.close();
		}

		components.clear();
		std::ifstream componentsFile(filepath + "components.txt");
		std::string rawId;
		std::string rawType;
		std::string rawPosX;
		std::string rawPosY;

		while (std::getline(componentsFile, rawId, ','))
		{
			std::getline(componentsFile, rawType, ',');
			std::getline(componentsFile, rawPosX, ',');
			std::getline(componentsFile, rawPosY, '\n');

			components.push_back({ stoi(rawId), rawType, olc::vi2d(stoi(rawPosX), stoi(rawPosY)) });
		}

		connections.clear();
		std::ifstream connectionsFile(filepath + "connections.txt");
		std::string rawConnectionId;
		std::string rawTerminalA;
		std::string rawTerminalB;
		std::string rawTerminalAPosX;
		std::string rawTerminalAPosY;
		std::string rawTerminalBPosX;
		std::string rawTerminalBPosY;
		std::string rawNotOutTerminal;

		while (std::getline(connectionsFile, rawConnectionId, ','))
		{
			std::getline(connectionsFile, rawTerminalA, ',');
			std::getline(connectionsFile, rawTerminalB, ',');
			std::getline(connectionsFile, rawTerminalAPosX, ',');
			std::getline(connectionsFile, rawTerminalAPosY, ',');
			std::getline(connectionsFile, rawTerminalBPosX, ',');
			std::getline(connectionsFile, rawTerminalBPosY, ',');
			std::getline(connectionsFile, rawNotOutTerminal, '\n');

			connections.push_back({
				stoi(rawConnectionId),
				stoi(rawTerminalA),
				stoi(rawTerminalB),
				olc::vi2d({ stoi(rawTerminalAPosX), stoi(rawTerminalAPosY) }),
				olc::vi2d({ stoi(rawTerminalBPosX), stoi(rawTerminalBPosY) }),
				stoi(rawNotOutTerminal)
			});
		}

		terminals.clear();
		std::ifstream terminalsFile(filepath + "terminals.txt");
		std::string rawTerminalId;
		std::string rawTerminalPosX;
		std::string rawTerminalPosY;
		std::string rawTerminalType;
		std::string rawTerminalComponentId;

		while (std::getline(terminalsFile, rawTerminalId, ','))
		{
			std::getline(terminalsFile, rawTerminalPosX, ',');
			std::getline(terminalsFile, rawTerminalPosY, ',');
			std::getline(terminalsFile, rawTerminalType, ',');
			std::getline(terminalsFile, rawTerminalComponentId, '\n');

			terminals.push_back({
				stoi(rawTerminalId),
				olc::vi2d({ stoi(rawTerminalPosX), stoi(rawTerminalPosY) }),
				FALSE,
				rawTerminalType[0],
				stoi(rawTerminalComponentId),
				});
		}

		updateSourceConnections();
		updateSimulation = TRUE;
	}

	void DrawComponents()
	{
		for (auto component : components)
		{
			olc::vi2d componentWorldPos = component.pos;
			olc::vi2d componentScreenPos;
			pz.WorldToScreen(componentWorldPos, componentScreenPos);

			if (component.type == "TRANSISTOR")
				DrawTransistor(componentScreenPos);

			if (component.type == "GATED LATCH")
			{
				DrawGatedLatch(componentScreenPos);


				olc::Pixel ledColour = olc::GREY;

				for (auto terminal : terminals)
				{
					if (terminal.componentId == component.id && terminal.type == 'Q' && terminal.state)
					{
						ledColour = olc::GREEN;
						break;
					}
				}

				DrawLed(componentScreenPos, ledColour);
			}
				

			if (component.type == "LED")
			{
				olc::Pixel ledColour = olc::GREY;

				for (auto terminal : terminals)
				{
					if (terminal.componentId == component.id && terminal.state)
					{
						ledColour = olc::GREEN;
						break;
					}
				}

				DrawLed(componentScreenPos, ledColour);
			}
		}
	}

	void DrawConnections()
	{
		for (auto connection : connections)
		{
			olc::Pixel colour = olc::DARK_GREY;
			olc::vi2d terminalAWorldPos = connection.terminalAPos;
			olc::vi2d terminalAScreenPos;
			pz.WorldToScreen(terminalAWorldPos, terminalAScreenPos);
			olc::vi2d terminalBWorldPos = connection.terminalBPos;
			olc::vi2d terminalBScreenPos;
			pz.WorldToScreen(terminalBWorldPos, terminalBScreenPos);


			if (connection.state)
				colour = olc::GREEN;

			DrawLine(terminalAScreenPos, terminalBScreenPos, colour);
		}
	}

	void DrawTerminals()
	{
		for (auto terminal : terminals)
		{
			olc::vi2d terminalWorldPos = terminal.pos;
			olc::vi2d terminalScreenPos;

			pz.WorldToScreen(terminalWorldPos, terminalScreenPos);

			olc::Pixel colour = olc::WHITE;

			if (terminal.state)
				colour = olc::GREEN;

			if (selectedTerminalA == terminal.id || selectedTerminalB == terminal.id)
				colour = olc::MAGENTA;

			DrawTerminal(terminalScreenPos, colour);
		}
	}

	void DrawStrings()
	{
		std::string offsetString = std::to_string(int(pz.GetOffset().x)) + ", " + std::to_string(int(pz.GetOffset().y));

		DrawString(olc::vi2d(50, 50), offsetString, olc::DARK_GREY);

		if (!placingModule)
			DrawString(olc::vi2d(50, 70), inventoryComponents[activeInventoryComponent], olc::GREEN);
		else
			DrawString(olc::vi2d(50, 70), inventoryModules[activeInventoryModule], olc::GREEN);

		if (simulationPaused)
			DrawString(olc::vi2d(50, 90), "PAUSED", olc::RED);
	}


	void DrawTransistor(olc::vi2d pos)
	{
		int size = 25;
		DrawLine(pos + olc::vi2d(0, -size), pos + olc::vi2d(0, size), olc::GREEN);
		DrawLine(pos, pos + olc::vi2d(-size, 0), olc::GREEN);
		DrawLine(pos + olc::vi2d(0, -size / 2), pos + olc::vi2d(size, -size), olc::GREEN);
		DrawLine(pos + olc::vi2d(0, size / 2), pos + olc::vi2d(size, size), olc::GREEN);
	}

	void DrawGatedLatch(olc::vi2d pos)
	{
		DrawRect(pos + olc::vi2d(-25, -25), { 50, 50 }, olc::GREEN);
	}

	void DrawSource(olc::vi2d pos)
	{
		DrawLine(pos + olc::vi2d(-25, 0), pos + olc::vi2d(-5, 0), olc::GREY);
		DrawLine(pos + olc::vi2d(-5, -20), pos + olc::vi2d(-5, 20), olc::GREY);
		DrawLine(pos + olc::vi2d(5, -30), pos + olc::vi2d(5, 30), olc::GREEN);
		DrawLine(pos + olc::vi2d(5, 0), pos + olc::vi2d(25, 0), olc::GREEN);
	}

	void DrawClock(olc::vi2d pos)
	{
		olc::Pixel clockColour = olc::GREY;
		if (clockState)
			clockColour = olc::GREEN;

		int xOffset;
		if (clockSpeed > 9)
			xOffset = -6;
		else
			xOffset = -3;

		DrawCircle(pos, 30, clockColour);
		DrawString(pos + olc::vi2d(xOffset, 15), std::to_string(clockSpeed), clockColour);
	}

	void DrawTerminal(olc::vi2d pos, olc::Pixel colour)
	{
		DrawCircle(pos, 3, colour);
	}

	void DrawLed(olc::vi2d pos, olc::Pixel colour)
	{
		for (int i = 1; i < 10; i++)
			DrawCircle(pos, i, colour);
	}

	double CalculateDistance(olc::vi2d a, olc::vi2d b)
	{
		return sqrt(pow(a.x - b.x, 2) + pow(a.y - b.y, 2));
	}

	olc::vi2d midpoint(olc::vi2d a, olc::vi2d b)
	{
		return a + ((b - a) / 2);
	}

	olc::vi2d calculateNotOut(olc::vi2d a, olc::vi2d b)
	{
		return b + ((a - b) / 6);
	}

	Connection* findConnectionByTerminalA(int terminalAId)
	{
		for (auto& connection : connections)
		{
			if (connection.terminalA == terminalAId)
				return &connection;
		}

		return nullptr;
	}

	Terminal* findTerminal(int id)
	{
		for (auto& terminal : terminals)
		{
			if (terminal.id == id)
				return &terminal;
		}

		return nullptr;
	}

	Terminal* findTerminalByComponent(int componentId, char terminalType)
	{
		for (auto& terminal : terminals)
		{
			if (terminal.componentId == componentId && terminal.type == terminalType)
				return &terminal;
		}

		return nullptr;
	}

	Terminal* simulateTransistor(int id)
	{
		bool baseState = FALSE;
		bool collectorState = FALSE;
		bool baseFound = FALSE;
		bool collectorFound = FALSE;
		Terminal* emitterTerminal = nullptr;
		Terminal* notOutTerminal = nullptr;

		for (auto& terminal : terminals)
		{
			if (terminal.componentId == id && terminal.type == 'C')
			{
				collectorFound = TRUE;

				if (terminal.state)
					collectorState = TRUE;
			}
				
			if (terminal.componentId == id && terminal.type == 'B')
			{
				baseFound = TRUE;

				if (terminal.state)
					baseState = TRUE;
			}

			if (terminal.componentId == id && terminal.type == 'E')
				emitterTerminal = &terminal;

			if (terminal.componentId == id && terminal.type == 'N')
				notOutTerminal = &terminal;

			if (collectorFound && baseFound && emitterTerminal && notOutTerminal)
				break;
		}

		if (baseState && collectorState)
		{
			return emitterTerminal;
		}
			
		
		if (!baseState && collectorState)
		{
			return notOutTerminal;
		}
			
		return nullptr;
	}

	void simulateGatedLatch(int id)
	{
		// gated latch: D = data in, W = write enable, Q = data out

		bool dataInState = FALSE;
		bool writeEnableState = FALSE;
		bool dataInFound = FALSE;
		bool writeEnableFound = FALSE;
		
		Terminal* dataOutTerminal = nullptr;

		for (auto& terminal : terminals)
		{
			if (terminal.componentId == id && terminal.type == 'D')
			{
				dataInFound = TRUE;

				if (terminal.state)
					dataInState = TRUE;
			}

			if (terminal.componentId == id && terminal.type == 'W')
			{
				writeEnableFound = TRUE;

				if (terminal.state)
					writeEnableState = TRUE;
			}

			if (terminal.componentId == id && terminal.type == 'Q')
				dataOutTerminal = &terminal;

			if (dataInFound && writeEnableFound && dataOutTerminal)
				break;
		}

		if (dataOutTerminal && writeEnableState)
		{
			dataOutTerminal->state = dataInState;
		}
	}

	void updateSourceConnections()
	{
		sourceConnections.clear();

		for (auto& connection : connections)
		{
			if (connection.terminalA == 1 || connection.terminalA == 3)
				sourceConnections.push_back(&connection);
			else
			{
				if (findTerminal(connection.terminalA)->type == 'Q' && findTerminal(connection.terminalA)->state)
					sourceConnections.push_back(&connection);
			}
		}
	}

	void print(std::string input)
	{
		std::cout << input << std::endl;
	}

	olc::vf2d GetWorldMouse()
	{
		olc::vf2d worldMouse;
		pz.ScreenToWorld(GetMousePos(), worldMouse);
		return worldMouse;
	}

	void PlaceOrGate(olc::vi2d pos)
	{
		// DrawCircle(pos, 3, colour);
	}

	void deleteClosest()
	{
		double smallestDistance = 0.00;
		int closestConnectionId = 0;
		int closestConnectionNotOutTerminalId = 0;
		bool noConnectionDeleted = TRUE;

		for (auto connection : connections)
		{
			olc::vi2d thisMidpoint = midpoint(connection.terminalAPos, connection.terminalBPos);
			double distance = CalculateDistance(thisMidpoint, GetWorldMouse());

			if (distance < smallestDistance || smallestDistance == 0.00)
			{

				smallestDistance = distance;
				closestConnectionId = connection.id;
				closestConnectionNotOutTerminalId = connection.notOutTerminal;
			}
		}

		if (smallestDistance < 10.00 && closestConnectionId)
		{
			// Delete connection not-out terminal
			for (auto iter = terminals.begin(); iter != terminals.end(); ++iter)
			{
				if (iter->id == closestConnectionNotOutTerminalId)
				{
					iter = terminals.erase(iter);
					break;
				}
			}

			// Delete connection
			for (auto iter = connections.begin(); iter != connections.end(); ++iter)
			{
				if (iter->id == closestConnectionId)
				{
					iter = connections.erase(iter);
					break;
				}
			}

			noConnectionDeleted = FALSE;
		}
		
		if (noConnectionDeleted)
		{
			selectedTerminalA = 0;
			selectedTerminalB = 0;
			int closestId = 0;

			for (auto component : components)
			{
				double distance = CalculateDistance(component.pos, GetWorldMouse());

				if (distance < smallestDistance || smallestDistance == 0.00)
				{
					smallestDistance = distance;
					closestId = component.id;
				}
			}

			if (smallestDistance < 10.00)
			{
				bool terminalsRemaining = TRUE;

				while (terminalsRemaining)
				{
					bool terminalsFound = FALSE;
					for (auto terminal : terminals)
					{
						if (terminal.componentId == closestId)
							terminalsFound = TRUE;
					}

					if (!terminalsFound)
						terminalsRemaining = FALSE;

					for (auto iter = terminals.begin(); iter != terminals.end(); ++iter)
					{
						if (iter->componentId == closestId)
						{
							bool connectionsRemaining = TRUE;

							while (connectionsRemaining)
							{
								bool connectionsFound = FALSE;
								for (auto connection : connections)
								{
									if (connection.terminalA == iter->id || connection.terminalB == iter->id)
										connectionsFound = TRUE;
								}

								if (!connectionsFound)
									connectionsRemaining = FALSE;

								for (auto connIter = connections.begin(); connIter != connections.end(); ++connIter)
								{
									if (connIter->terminalA == iter->id || connIter->terminalB == iter->id)
									{
										if (connIter->notOutTerminal)
										{
											for (auto notOutTerminalIter = terminals.begin(); notOutTerminalIter != terminals.end(); ++notOutTerminalIter)
											{
												if (notOutTerminalIter->id == connIter->notOutTerminal)
												{
													notOutTerminalIter = terminals.erase(notOutTerminalIter);
													break;
												}
											}
										}
											
										connIter = connections.erase(connIter);
										break;
									}
								}
							}
							

							iter = terminals.erase(iter);
							break;
						}
					}
				}
				

				for (auto iter = components.begin(); iter != components.end(); ++iter)
				{
					if (iter->id == closestId)
					{
						iter = components.erase(iter);
						break;
					}
				}
			}
		}

		updateSourceConnections();
		updateSimulation = TRUE;
	}

	void simulateClock()
	{
		if (clockSpeed)
		{
			clockTicks++;
			
			if (clockTicks >= clockSpeed * 100 || clockSpeed == 1)
			{
				if (clockState)
					clockState = FALSE;
				else
					clockState = TRUE;

				clockTicks = 0;
				updateSimulation = TRUE;
				redrawRequired = TRUE;
			}
		}
		else
		{
			if (GetKey(olc::Key::O).bReleased)
			{
				if (clockState)
					clockState = FALSE;
				else
					clockState = TRUE;

				updateSimulation = TRUE;
				redrawRequired = TRUE;
			}
		}
	}
		
};

int main()
{
	Viscom vc;
	if (vc.Construct(1600, 800, 1, 1))
		vc.Start();

	return 0;
}
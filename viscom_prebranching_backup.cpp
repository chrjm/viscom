#define OLC_PGE_APPLICATION
#include "olcPixelGameEngine.h"
#include <vector>

struct Terminal
{
	int id;
	olc::vi2d pos;
	bool state = FALSE;
	char type; // S = source start, Z = source end, C = collector, B = base, E = emitter
	int transistorId;
};

struct Component
{
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
		terminals.push_back({ 1, olc::vi2d(500 + 25, 50), TRUE, 'S', 1 });
		terminals.push_back({ 2, olc::vi2d(500 - 25, 50), FALSE, 'Z', 1 });
		return true;
	}

	bool OnUserUpdate(float fElapsedTime) override
	{
		Clear(olc::BLACK);

		DrawSource(olc::vi2d(ScreenWidth() / 2, 50));

		
		// --------------------
		// Run Logic Simulation

		if (connections.size() > 0)
		{
			for (auto currentConnection : sourceConnections)
			{
				bool finished = FALSE;

				while (!finished)
				{
					currentConnection->state = TRUE;

					Terminal* currentTerminalB = findTerminal(currentConnection->terminalB);

					if (currentTerminalB)
					{
						currentTerminalB->state = TRUE;

						/*
						// Find all connections beginning from this transistor's emitter
						std::vector<Connection*> nextConnections;

						for (auto& connection : connections)
						{
							if (connection.terminalA == currentTerminalB->id)
								nextConnections.push_back(&connection);
						}

						for (auto& nextConnection : nextConnections)
						{

						}
						*/

						Connection* nextConnection = findConnectionByTerminalA(currentTerminalB->id);

						if (nextConnection)
							currentConnection = nextConnection;
						else
						{
							if (currentTerminalB->type == 'C')
							{
								Terminal* emitterTerminal = simulateTransistor(currentTerminalB->transistorId);

								if (emitterTerminal)
								{
									emitterTerminal->state = TRUE;

									nextConnection = findConnectionByTerminalA(emitterTerminal->id);

									if (nextConnection)
										currentConnection = nextConnection;
									else
										finished = TRUE;
								}
								else
									finished = TRUE;
								
							}
							else
								finished = TRUE;
						}
					}
					else
						finished = TRUE;
				}
			}
			
		}

		//---------------------
		
		if (GetMouse(0).bReleased)
		{
			olc::vi2d pos = olc::vi2d(GetMouseX(), GetMouseY());
			components.push_back({ inventoryItems[activeInventoryItem], pos });

			if (inventoryItems[activeInventoryItem] == "TRANSISTOR")
			{
				terminals.push_back({ lastTerminalId, pos + olc::vi2d(25, -25), FALSE, 'C', lastTransistorId });
				lastTerminalId++;
				terminals.push_back({ lastTerminalId, pos + olc::vi2d(-25, 0), FALSE, 'B', lastTransistorId });
				lastTerminalId++;
				terminals.push_back({ lastTerminalId, pos + olc::vi2d(25, 25), FALSE, 'E', lastTransistorId });
				lastTerminalId++;

				lastTransistorId++;
			}
		}


		if (GetMouse(1).bReleased)
		{
			if (!selectedTerminalA)
			{
				double smallestDistance = 0.00;
				int closestTerminalId = 0;
				olc::vi2d closestTerminalPos;

				for (auto terminal : terminals)
				{
					double distance = CalculateDistance(terminal.pos, olc::vi2d(GetMouseX(), GetMouseY()));

					if (distance < smallestDistance || smallestDistance == 0.00)
					{
						smallestDistance = distance;
						closestTerminalId = terminal.id;
						closestTerminalPos = terminal.pos;
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
					double distance = CalculateDistance(terminal.pos, olc::vi2d(GetMouseX(), GetMouseY()));

					if (distance < smallestDistance || smallestDistance == 0.00)
					{
						smallestDistance = distance;
						closestTerminalId = terminal.id;
						closestTerminalPos = terminal.pos;
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
				Connection newConnection = { 
					lastConnectionId,
					selectedTerminalA,
					selectedTerminalB,
					selectedTerminalAPos,
					selectedTerminalBPos
				};
				lastConnectionId++;
				connections.push_back(newConnection);
				updateSourceConnections();
				selectedTerminalA = 0;
				selectedTerminalB = 0;
				selectedTerminalAPos = olc::vi2d(0, 0);
				selectedTerminalBPos = olc::vi2d(0, 0);
				
			}
			
		}

		
		if (GetKey(olc::Key::SPACE).bReleased)
		{
			if (activeInventoryItem < (int) inventoryItems.size() - 1)
				activeInventoryItem++;
			else
				activeInventoryItem = 0;
		}

		for (auto component : components)
		{
			olc::Pixel colour = olc::GREEN;

			if (component.type == "LOGIC END")
				colour = olc::RED;

			if (component.type == "LOGIC START")
				DrawCircle(component.pos, 5, colour);
			else if (component.type == "LOGIC END")
			{
				DrawCircle(component.pos, 5, colour);
			}
			else if (component.type == "TRANSISTOR")
				DrawTransistor(component.pos);
		}

		for (auto connection : connections)
		{
			olc::Pixel colour = olc::DARK_GREY;

			if (connection.state)
				colour = olc::GREEN;

			DrawLine(connection.terminalAPos, connection.terminalBPos, colour);
		}

		
		for (auto terminal : terminals)
		{
			olc::Pixel colour = olc::WHITE;

			if (terminal.state)
				colour = olc::GREEN;

			if (selectedTerminalA == terminal.id || selectedTerminalB == terminal.id)
				colour = olc::MAGENTA;
			
			DrawTerminal(terminal.pos, colour);
		}
		
		DrawString(olc::vi2d(50, 50), inventoryItems[activeInventoryItem], olc::GREEN);

		return true;
	}

private:
	std::vector<Component> components;
	std::vector<Connection> connections;
	std::vector<Connection*> sourceConnections;
	std::vector<Terminal> terminals;
	int lastTerminalId = 3;
	int lastConnectionId = 1;
	int lastTransistorId = 2;
	int selectedTerminalA = 0;
	int selectedTerminalB = 0;
	olc::vi2d selectedTerminalAPos;
	olc::vi2d selectedTerminalBPos;
	int activeInventoryItem = 0;
	std::vector<std::string> inventoryItems = {
		"TRANSISTOR",
		"LOGIC START",
		"LOGIC END",
	};

	void DrawTransistor(olc::vi2d pos)
	{
		int size = 25;
		DrawLine(pos + olc::vi2d(0, -size), pos + olc::vi2d(0, size), olc::GREEN);
		DrawLine(pos, pos + olc::vi2d(-size, 0), olc::GREEN);
		DrawLine(pos + olc::vi2d(0, -size / 2), pos + olc::vi2d(size, -size), olc::GREEN);
		DrawLine(pos + olc::vi2d(0, size / 2), pos + olc::vi2d(size, size), olc::GREEN);
	}

	void DrawSource(olc::vi2d pos)
	{
		DrawLine(pos + olc::vi2d(-25, 0), pos + olc::vi2d(-5, 0), olc::GREY);
		DrawLine(pos + olc::vi2d(-5, -20), pos + olc::vi2d(-5, 20), olc::GREY);
		DrawLine(pos + olc::vi2d(5, -30), pos + olc::vi2d(5, 30), olc::GREEN);
		DrawLine(pos + olc::vi2d(5, 0), pos + olc::vi2d(25, 0), olc::GREEN);
	}

	void DrawTerminal(olc::vi2d pos, olc::Pixel colour)
	{
		DrawCircle(pos, 3, colour);
	}

	double CalculateDistance(olc::vi2d a, olc::vi2d b)
	{
		//
		return sqrt(pow(a.x - b.x, 2) + pow(a.y - b.y, 2));
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

	Terminal* simulateTransistor(int id)
	{
		bool baseState = FALSE;
		bool collectorState = FALSE;
		bool baseFound = FALSE;
		bool collectorFound = FALSE;
		Terminal* emitterTerminal = nullptr;

		for (auto& terminal : terminals)
		{
			if (terminal.transistorId == id && terminal.type == 'C')
			{
				collectorFound = TRUE;

				if (terminal.state)
					collectorState = TRUE;
			}
				
			if (terminal.transistorId == id && terminal.type == 'B')
			{
				baseFound = TRUE;

				if (terminal.state)
					baseState = TRUE;
			}

			if (terminal.transistorId == id && terminal.type == 'E')
				emitterTerminal = &terminal;

			if (collectorFound && baseFound && emitterTerminal)
				break;
			
		}

		if (baseState && collectorState)
			return emitterTerminal;

		return nullptr;
	}

	void updateSourceConnections()
	{
		for (auto& connection : connections)
		{
			if (connection.terminalA == 1)
				sourceConnections.push_back(&connection);
		}
	}
};

int main()
{
	Viscom vc;
	if (vc.Construct(1000, 500, 1, 1))
		vc.Start();

	return 0;
}
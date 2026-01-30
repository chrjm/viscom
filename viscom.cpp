#define OLC_PGE_APPLICATION
#define OLC_PGEX_PANZOOM

#include <fstream>
#include <iomanip>
#include <vector>

#include "olcPixelGameEngine.h"
#include "olcPGEX_PanZoom.h"

// --- Terminal Types ------------------------------------------------------------------------
// sourceStart, sourceEnd, clock, clockHalt, buffer, transCollector, transBase, transEmitter
// transNotOut, gatedIn, gatedWriteEnable, gatedOut
// ramIn1-8, ramOut1-8, ramWriteEnable, ramAddressIn1-4, bus1-8
// counterIn1-4, counterOut1-4, counterClock, counterWriteEnable, counterCountEnable,
// microcounterIn1-3, microcounterOut1-3, microcounterClock, microcounterReset
// IRIn1-8, IRIn1-4, IRDecodeOut5-8, IRWriteEnable, displayIn1-8
// decoderIn1-9, decoderOut1-17, flagsRegIn1-2, flagsRegOut1-2, flagsRegWriteEnable
// aluInA1-8, aluInB1-8, aluOut1-8, aluSub, aluZeroFlagOut, aluCarryFlagOut

struct Terminal
{
	int id;
	olc::vi2d pos;
	bool state = false;
	std::string type;
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
	bool state = false;
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
			terminals.push_back({ 1, { 25, 0 }, true, "sourceStart", 1 });
			terminals.push_back({ 2, { -25, 0 }, false, "sourceEnd", 1 });
			terminals.push_back({ 3, { -200, 0 }, false, "clock", 1 });
			terminals.push_back({ lastTerminalId, { -200, -20 }, false, "clockHalt", 0 });
			lastTerminalId++;
		}

		return true;
	}

	bool OnUserUpdate(float fElapsedTime) override
	{
		if (GetKey(olc::P).bReleased)
		{
			if (!simulationPaused)
				simulationPaused = true;
			else
				simulationPaused = false;

			redrawRequired = true;
		}

		if (GetKey(olc::I).bReleased)
		{
			if (!showInfo)
				showInfo = true;
			else
				showInfo = false;

			redrawRequired = true;
		}

		simulateClock();

		if (GetKey(olc::Key::UP).bReleased)
		{
			clockSpeed++;
			redrawRequired = true;
		}

		if (GetKey(olc::Key::DOWN).bReleased)
		{
			if (clockSpeed > 0)
			{
				clockSpeed--;
				redrawRequired = true;
			}
		}


		if (GetMouseWheel() > 0)
		{
			pz.ZoomIn(1.1f);
			redrawRequired = true;
		}

		if (GetMouseWheel() < 0)
		{
			pz.ZoomOut(0.9f);
			redrawRequired = true;
		}

		if (GetKey(olc::Z).bReleased)
		{
			pz.SetScale({ 1.0f, 1.0f });
			redrawRequired = true;
		}

		if (GetKey(olc::Q).bHeld)
		{
			pz.ZoomOut(1000);
			redrawRequired = true;
		}

		// Pan
		if (GetMouse(2).bHeld)
			redrawRequired = true;

		if (GetMouse(2).bPressed)
			pz.StartPan();

		if (GetMouse(2).bReleased)
			pz.StopPan();

		// Draw mouse guides
		if (GetKey(olc::X).bHeld || GetKey(olc::X).bReleased)
		{
			redrawRequired = true;
		}


		pz.Update(fElapsedTime);

		if (GetKey(olc::Key::R).bReleased)
		{
			programRAM();
			updateSimulation = true;
			redrawRequired = true;
		}

		// -------------------------------------------------------------------
		// Run Logic Simulation
		//
		//

		std::vector<int> newVisitedTerminalIds;
		std::vector<int> newVisitedTerminalIdsTwo;

		std::vector<int> previousTerminalsState = {};
		for (auto terminal : terminals)
		{
			previousTerminalsState.push_back(terminal.state);
		}

		if (updateSimulation && !simulationPaused)
		{
			redrawRequired = true;

			for (auto& connection : connections)
				connection.state = false;

			for (auto& terminal : terminals)
				if (terminal.type != "sourceStart" && terminal.type != "gatedOut" && !("aluOut1" <= terminal.type && terminal.type <= "aluOut8") && !("ramOut1" <= terminal.type && terminal.type <= "ramOut8") && !("counterOut1" <= terminal.type && terminal.type <= "counterOut4") && !("microcounterOut1" <= terminal.type && terminal.type <= "microcounterOut3") && !("IROut1" <= terminal.type && terminal.type <= "IROut4") && !("IRDecodeOut5" <= terminal.type && terminal.type <= "IRDecodeOut8") && !(terminal.type.find("decoderOut") != std::string::npos) && !(terminal.type == "flagsRegOut1") && !(terminal.type == "flagsRegOut2") && !(terminal.type == "aluZeroFlagOut") && !(terminal.type == "aluCarryFlagOut"))
				{
					terminal.state = false;
				}
				else if (terminal.type == "clock")
					terminal.state = clockState;

			counterCounted = false;
			microcounterCounted = false;

			std::vector<int> transistorsToSimulate;
			std::vector<int> gatedLatchesToSimulate;
			std::vector<Terminal*> activeTerminals;
			std::vector<Connection*> activeConnections;

			updateSourceConnections();

			for (auto currentConnection : sourceConnections)
			{
				if (currentConnection->terminalA == 1)
					currentConnection->state = true;

				if (currentConnection->terminalA == 3)
					currentConnection->state = clockState;

				Terminal* currentTerminalA = findTerminal(currentConnection->terminalA);
				Terminal* currentTerminalB = findTerminal(currentConnection->terminalB);

				newVisitedTerminalIds.push_back(currentConnection->terminalA);

				if ("aluOut1" <= currentTerminalA->type && currentTerminalA->type <= "aluOut8")
					currentConnection->state = currentTerminalA->state;

				if ("ramOut1" <= currentTerminalA->type && currentTerminalA->type <= "ramOut8")
					currentConnection->state = currentTerminalA->state;

				if ("counterOut1" <= currentTerminalA->type && currentTerminalA->type <= "counterOut4")
					currentConnection->state = currentTerminalA->state;

				if ("microcounterOut1" <= currentTerminalA->type && currentTerminalA->type <= "microcounterOut3")
					currentConnection->state = currentTerminalA->state;

				if ("IROut1" <= currentTerminalA->type && currentTerminalA->type <= "IROut4")
					currentConnection->state = currentTerminalA->state;

				if ("IRDecodeOut5" <= currentTerminalA->type && currentTerminalA->type <= "IRDecodeOut8")
					currentConnection->state = currentTerminalA->state;

				if (currentTerminalA->type.find("decoderOut") != std::string::npos)
					currentConnection->state = currentTerminalA->state;

				if ("flagsRegOut1" <= currentTerminalA->type && currentTerminalA->type <= "flagsRegOut2")
					currentConnection->state = currentTerminalA->state;

				if (currentTerminalA->type == "aluZeroFlagOut" || currentTerminalA->type == "aluCarryFlagOut")
					currentConnection->state = currentTerminalA->state;

				if (currentTerminalA->type == "gatedOut")
					gatedLatchesToSimulate.push_back(currentTerminalA->componentId);

				if (currentTerminalB)
				{
					newVisitedTerminalIds.push_back(currentConnection->terminalB);
					currentTerminalB->state = currentConnection->state;

					if (currentTerminalB->type == "buffer")
						activeTerminals.push_back(currentTerminalB);
					else if (currentTerminalB->type == "transCollector" || currentTerminalB->type == "transEmitter" || currentTerminalB->type == "transBase")
						transistorsToSimulate.push_back(currentTerminalB->componentId);
					else if (currentTerminalB->type == "gatedIn" || currentTerminalB->type == "gatedWriteEnable" || currentTerminalB->type == "gatedOut")
						gatedLatchesToSimulate.push_back(currentTerminalB->componentId);

				}
			}

			while ((transistorsToSimulate.size() || gatedLatchesToSimulate.size() || activeTerminals.size()))
			{
				for (auto transistorId : transistorsToSimulate)
				{
					Terminal* thisNotOut = findTerminalByComponent(transistorId, "transNotOut");
					Terminal* thisEmitter = findTerminalByComponent(transistorId, "transEmitter");

					if (thisNotOut)
					{
						thisNotOut->state = false;
						activeTerminals.push_back(thisNotOut);
					}

					if (thisEmitter)
					{
						thisEmitter->state = false;
						activeTerminals.push_back(thisEmitter);
					}

					Terminal* thisNextTerminal = simulateTransistor(transistorId);

					if (thisNextTerminal)
					{
						thisNextTerminal->state = true;
						activeTerminals.push_back(thisNextTerminal);
					}
				}

				transistorsToSimulate.clear();

				for (auto gatedLatchId : gatedLatchesToSimulate)
				{
					Terminal* thisDataOut = findTerminalByComponent(gatedLatchId, "gatedOut");

					bool originalDataOutState = false;
					if (thisDataOut->state)
						originalDataOutState = true;

					simulateGatedLatch(gatedLatchId);
					activeTerminals.push_back(thisDataOut);
				}

				gatedLatchesToSimulate.clear();				

				for (auto terminal : activeTerminals)
				{
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

					if (std::find(newVisitedTerminalIds.begin(), newVisitedTerminalIds.end(), connection->terminalB) != newVisitedTerminalIds.end() && 1 == 2)					
						newVisitedTerminalIds.erase(std::remove(newVisitedTerminalIds.begin(), newVisitedTerminalIds.end(), connection->terminalB), newVisitedTerminalIds.end());
					else
					{
						newVisitedTerminalIds.push_back(connection->terminalB);

						if (currentTerminalB)
						{
							int oldTerminalBState = currentTerminalB->state;
							currentTerminalB->state = connection->state;
							int newTerminalBState = currentTerminalB->state;


							if (oldTerminalBState != newTerminalBState)
							{
								if (currentTerminalB->type == "buffer")
									activeTerminals.push_back(currentTerminalB);
								else if (currentTerminalB->type == "transBase" || currentTerminalB->type == "transCollector" || currentTerminalB->type == "transEmitter")
									transistorsToSimulate.push_back(currentTerminalB->componentId);
								else if (currentTerminalB->type == "gatedIn" || currentTerminalB->type == "gatedWriteEnable" || currentTerminalB->type == "gatedOut")
									gatedLatchesToSimulate.push_back(currentTerminalB->componentId);
								else if ("bus1" <= currentTerminalB->type && currentTerminalB->type <= "bus8")
								{
									for (auto& terminal : terminals)
									{
										if (terminal.type == currentTerminalB->type)
										{
											int oldState = terminal.state;
											terminal.state = currentTerminalB->state;
											activeTerminals.push_back(&terminal);
											int newState = terminal.state;

											if (oldState != newState)
											{

											}
										}
									}
								}
							}	
						}
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
								terminal.state = true;
							}
						}
					}
				}

				// Simulate dynamic components
				simulateALU();
				simulateCounter();
				simulateMicrocounter();
				simulateIR();
				simulateDecoder();
				simulateFlagsReg();
				simulateDisplay();
				simulateRAM();
				updateSourceConnections();
			}

			std::vector<int> newTerminalsState = {};
			for (auto terminal : terminals)
			{
				newTerminalsState.push_back(terminal.state);
			}

			bool somethingChanged = false;
			for (int i = 0; i < newTerminalsState.size(); i++)
			{
				if (previousTerminalsState[i] != newTerminalsState[i])
				{
					somethingChanged = true;
					break;
				}
			}

			if (!somethingChanged)
				updateSimulation = false;
		}

		//---------------------

		if (GetMouse(0).bReleased)
		{
			if (!placingModule)
			{
				components.push_back({ lastComponentId, inventoryComponents[activeInventoryComponent], GetWorldMouse() });

				if (inventoryComponents[activeInventoryComponent] == "TRANSISTOR")
				{
					terminals.push_back({ lastTerminalId, GetWorldMouse() + olc::vi2d(25, -25), false, "transCollector", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, GetWorldMouse() + olc::vi2d(-25, 0), false, "transBase", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, GetWorldMouse() + olc::vi2d(25, 25), false, "transEmitter", lastComponentId });
					lastTerminalId++;
				}

				if (inventoryComponents[activeInventoryComponent] == "GATED LATCH")
				{
					terminals.push_back({ lastTerminalId, GetWorldMouse() + olc::vi2d(-25, -25), false, "gatedIn", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, GetWorldMouse() + olc::vi2d(-25, 25), false, "gatedWriteEnable", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, GetWorldMouse() + olc::vi2d(25, 0), false, "gatedOut", lastComponentId });
					lastTerminalId++;
				}

				if (inventoryComponents[activeInventoryComponent] == "ALU")
				{
					float margin = 27.3;
					int width = 300;
					olc::vf2d worldMouse = GetWorldMouse();

					terminals.push_back({ lastTerminalId, worldMouse + olc::vi2d(width, width / 2), false, "aluSub", lastComponentId });
					lastTerminalId++;

					terminals.push_back({ lastTerminalId, worldMouse + olc::vi2d(width, width / 6), false, "aluZeroFlagOut", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vi2d(width, width / 4), false, "aluCarryFlagOut", lastComponentId });
					lastTerminalId++;

					terminals.push_back({ lastTerminalId, worldMouse + olc::vi2d(margin * 2 + 0 * margin, 0), false, "aluInA1", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vi2d(margin * 2 + 1 * margin, 0), false, "aluInA2", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vi2d(margin * 2 + 2 * margin, 0), false, "aluInA3", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vi2d(margin * 2 + 3 * margin, 0), false, "aluInA4", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vi2d(margin * 2 + 4 * margin, 0), false, "aluInA5", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vi2d(margin * 2 + 5 * margin, 0), false, "aluInA6", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vi2d(margin * 2 + 6 * margin, 0), false, "aluInA7", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vi2d(margin * 2 + 7 * margin, 0), false, "aluInA8", lastComponentId });
					lastTerminalId++;

					terminals.push_back({ lastTerminalId, worldMouse + olc::vi2d(margin * 2 + 0 * margin, margin * 3 + margin * 8), false, "aluInB1", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vi2d(margin * 2 + 1 * margin, margin * 3 + margin * 8), false, "aluInB2", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vi2d(margin * 2 + 2 * margin, margin * 3 + margin * 8), false, "aluInB3", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vi2d(margin * 2 + 3 * margin, margin * 3 + margin * 8), false, "aluInB4", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vi2d(margin * 2 + 4 * margin, margin * 3 + margin * 8), false, "aluInB5", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vi2d(margin * 2 + 5 * margin, margin * 3 + margin * 8), false, "aluInB6", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vi2d(margin * 2 + 6 * margin, margin * 3 + margin * 8), false, "aluInB7", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vi2d(margin * 2 + 7 * margin, margin * 3 + margin * 8), false, "aluInB8", lastComponentId });
					lastTerminalId++;

					terminals.push_back({ lastTerminalId, worldMouse + olc::vi2d(0, margin * 2 + 0 * margin), false, "aluOut1", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vi2d(0, margin * 2 + 1 * margin), false, "aluOut2", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vi2d(0, margin * 2 + 2 * margin), false, "aluOut3", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vi2d(0, margin * 2 + 3 * margin), false, "aluOut4", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vi2d(0, margin * 2 + 4 * margin), false, "aluOut5", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vi2d(0, margin * 2 + 5 * margin), false, "aluOut6", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vi2d(0, margin * 2 + 6 * margin), false, "aluOut7", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vi2d(0, margin * 2 + 7 * margin), false, "aluOut8", lastComponentId });
					lastTerminalId++;
				}

				if (inventoryComponents[activeInventoryComponent] == "RAM")
				{
					float scale = pz.GetScale().x;
					int margin = 20 * scale;
					int ramHeight = 415 * scale;
					int ramWidth = 215 * scale;
					olc::vf2d worldMouse = GetWorldMouse();

					// Input Terminals
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(margin * 1 + 0 * margin, 0), false, "ramIn1", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(margin * 1 + 1.25 * margin, 0), false, "ramIn2", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(margin * 1 + 2.5 * margin, 0), false, "ramIn3", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(margin * 1 + 3.75 * margin, 0), false, "ramIn4", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(margin * 1 + 5 * margin, 0), false, "ramIn5", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(margin * 1 + 6.25 * margin, 0), false, "ramIn6", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(margin * 1 + 7.5 * margin, 0), false, "ramIn7", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(margin * 1 + 8.75 * margin, 0), false, "ramIn8", lastComponentId });
					lastTerminalId++;

					// Output Terminals
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(margin * 1 + 0 * margin, ramHeight), false, "ramOut1", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(margin * 1 + 1.25 * margin, ramHeight), false, "ramOut2", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(margin * 1 + 2.5 * margin, ramHeight), false, "ramOut3", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(margin * 1 + 3.75 * margin, ramHeight), false, "ramOut4", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(margin * 1 + 5 * margin, ramHeight), false, "ramOut5", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(margin * 1 + 6.25 * margin, ramHeight), false, "ramOut6", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(margin * 1 + 7.5 * margin, ramHeight), false, "ramOut7", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(margin * 1 + 8.75 * margin, ramHeight), false, "ramOut8", lastComponentId });
					lastTerminalId++;

					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(0, ramHeight / 2 + 20), false, "ramWriteEnable", lastComponentId });
					lastTerminalId++;

					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(ramWidth, ramHeight / 2 - 60), false, "ramAddressIn4", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(ramWidth, ramHeight / 2 - 20), false, "ramAddressIn3", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(ramWidth, ramHeight / 2 + 20), false, "ramAddressIn2", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(ramWidth, ramHeight / 2 + 60), false, "ramAddressIn1", lastComponentId });
					lastTerminalId++;
				}

				if (inventoryComponents[activeInventoryComponent] == "COUNTER")
				{
					float scale = pz.GetScale().x;
					int counterWidth = 120 * scale;
					int counterHeight = 50 * scale;
					float bitPadding = 25 * scale;
					olc::vf2d worldMouse = GetWorldMouse();

					// Counter In
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding - 4, 0), false, "counterIn4", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 2 - 4, 0), false, "counterIn3", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 3 - 4, 0), false, "counterIn2", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 4 - 4, 0), false, "counterIn1", lastComponentId });
					lastTerminalId++;

					// Counter Out
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding - 4, counterHeight), false, "counterOut4", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 2 - 4, counterHeight), false, "counterOut3", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 3 - 4, counterHeight), false, "counterOut2", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 4 - 4, counterHeight), false, "counterOut1", lastComponentId });
					lastTerminalId++;

					// Counter Clock
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(0, 0 + bitPadding / 3), false, "counterClock", lastComponentId });
					lastTerminalId++;

					// Jump (Counter Write Enable)
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(0, counterHeight / 2), false, "counterWriteEnable", lastComponentId });
					lastTerminalId++;

					// Count Enable (increment)
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(0, counterHeight - bitPadding / 3), false, "counterCountEnable", lastComponentId });
					lastTerminalId++;
				}

				if (inventoryComponents[activeInventoryComponent] == "MICROCOUNTER")
				{
					float scale = pz.GetScale().x;
					int counterWidth = 120 * scale;
					int counterHeight = 50 * scale;
					float bitPadding = 25 * scale;
					olc::vf2d worldMouse = GetWorldMouse();

					// Reset
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(0, counterHeight / 2), false, "microcounterReset", lastComponentId });
					lastTerminalId++;

					// Counter In
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 2 - 29, 0), false, "microcounterIn3", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 3 - 29, 0), false, "microcounterIn2", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 4 - 29, 0), false, "microcounterIn1", lastComponentId });
					lastTerminalId++;

					// Counter Out
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 2 - 29, counterHeight), false, "microcounterOut3", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 3 - 29, counterHeight), false, "microcounterOut2", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 4 - 29, counterHeight), false, "microcounterOut1", lastComponentId });
					lastTerminalId++;
				}

				if (inventoryComponents[activeInventoryComponent] == "IR")
				{
					float scale = pz.GetScale().x;
					int counterWidth = 240 * scale;
					int counterHeight = 50 * scale;
					float bitPadding = 25 * scale;
					float xOffset = -6 * scale;
					olc::vf2d worldMouse = GetWorldMouse();

					// IR In
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding - xOffset, 0), false, "IRIn8", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 2 - xOffset, 0), false, "IRIn7", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 3 - xOffset, 0), false, "IRIn6", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 4 - xOffset, 0), false, "IRIn5", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 5 - xOffset, 0), false, "IRIn4", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 6 - xOffset, 0), false, "IRIn3", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 7 - xOffset, 0), false, "IRIn2", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 8 - xOffset, 0), false, "IRIn1", lastComponentId });
					lastTerminalId++;

					// IR Decode Out
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding - xOffset, counterHeight), false, "IRDecodeOut8", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 2 - xOffset, counterHeight), false, "IRDecodeOut7", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 3 - xOffset, counterHeight), false, "IRDecodeOut6", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 4 - xOffset, counterHeight), false, "IRDecodeOut5", lastComponentId });
					lastTerminalId++;

					// IR Out
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 5- xOffset, counterHeight), false, "IROut4", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 6 - xOffset, counterHeight), false, "IROut3", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 7 - xOffset, counterHeight), false, "IROut2", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 8 - xOffset, counterHeight), false, "IROut1", lastComponentId });
					lastTerminalId++;

					// IR Write Enable
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(0, counterHeight / 2), false, "IRWriteEnable", lastComponentId });
					lastTerminalId++;

					
				}

				if (inventoryComponents[activeInventoryComponent] == "DECODER")
				{
					float scale = pz.GetScale().x;
					int width = 300 * scale;
					int height = 60 * scale;
					float bitPadding = 25 * scale;
					float topBitPadding = 50 * scale;
					float xOffset = 25 * scale;
					olc::vf2d worldMouse = GetWorldMouse();

					// Decoder In
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(topBitPadding - xOffset, 0), false, "decoderIn1", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(topBitPadding * 2 - xOffset, 0), false, "decoderIn2", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(topBitPadding * 3 - xOffset, 0), false, "decoderIn3", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(topBitPadding * 4 - xOffset, 0), false, "decoderIn4", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(topBitPadding * 5 - xOffset, 0), false, "decoderIn5", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(topBitPadding * 6 - xOffset, 0), false, "decoderIn6", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(topBitPadding * 7 - xOffset, 0), false, "decoderIn7", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(topBitPadding * 8 - xOffset, 0), false, "decoderIn8", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(topBitPadding * 9 - xOffset, 0), false, "decoderIn9", lastComponentId });
					lastTerminalId++;

					// Decoder Out
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding, height), false, "decoderOut1", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 2, height), false, "decoderOut2", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 3, height), false, "decoderOut3", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 4, height), false, "decoderOut4", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 5, height), false, "decoderOut5", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 6, height), false, "decoderOut6", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 7, height), false, "decoderOut7", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 8, height), false, "decoderOut8", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 9, height), false, "decoderOut9", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 10, height), false, "decoderOut10", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 11, height), false, "decoderOut11", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 12, height), false, "decoderOut12", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 13, height), false, "decoderOut13", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 14, height), false, "decoderOut14", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 15, height), false, "decoderOut15", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 16, height), false, "decoderOut16", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 17, height), false, "decoderOut17", lastComponentId });
					lastTerminalId++;
				}

				if (inventoryComponents[activeInventoryComponent] == "FLAGSREG")
				{
					float scale = pz.GetScale().x;
					int width = 75 * scale;
					int height = 60 * scale;
					float bitPadding = 25 * scale;
					float xOffset = 0 * scale;
					olc::vf2d worldMouse = GetWorldMouse();

					// Flags Write Enable
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(width, height / 2), false, "flagsRegWriteEnable", lastComponentId });
					lastTerminalId++;

					// Flags Reg In
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding - xOffset, 0), false, "flagsRegIn1", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 2 - xOffset, 0), false, "flagsRegIn2", lastComponentId });
					lastTerminalId++;

					// Flags Reg Out
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding - xOffset, height), false, "flagsRegOut1", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 2 - xOffset, height), false, "flagsRegOut2", lastComponentId });
					lastTerminalId++;
				}

				if (inventoryComponents[activeInventoryComponent] == "DISPLAY")
				{
					float scale = pz.GetScale().x;
					int width = 405 * scale;
					int height = 180 * scale;
					float bitPadding = 17 * scale;
					olc::vf2d worldMouse = GetWorldMouse();

					// Display In
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding, 0), false, "displayIn8", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 2, 0), false, "displayIn7", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 3, 0), false, "displayIn6", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 4, 0), false, "displayIn5", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 5, 0), false, "displayIn4", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 6, 0), false, "displayIn3", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 7, 0), false, "displayIn2", lastComponentId });
					lastTerminalId++;
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(bitPadding * 8, 0), false, "displayIn1", lastComponentId });
					lastTerminalId++;

					// Display Write Enable
					terminals.push_back({ lastTerminalId, worldMouse + olc::vf2d(width, height / 2), false, "displayWriteEnable", lastComponentId });
					lastTerminalId++;
				}

				if (inventoryComponents[activeInventoryComponent] == "BUFFER")
				{
					terminals.push_back({ lastTerminalId, GetWorldMouse(), false, "buffer", lastComponentId });
					lastTerminalId++;
				}

				if (inventoryComponents[activeInventoryComponent] == "BUSTERM")
				{
					std::string termType = "";
					if (currentBusColumn == 1)
						termType = "bus1";
					else if (currentBusColumn == 2)
						termType = "bus2";
					else if (currentBusColumn == 3)
						termType = "bus3";
					else if (currentBusColumn == 4)
						termType = "bus4";
					else if (currentBusColumn == 5)
						termType = "bus5";
					else if (currentBusColumn == 6)
						termType = "bus6";
					else if (currentBusColumn == 7)
						termType = "bus7";
					else if (currentBusColumn == 8)
						termType = "bus8";
					
					terminals.push_back({ lastTerminalId, GetWorldMouse(), false, termType, lastComponentId });
					lastTerminalId++;
				}

				if (inventoryComponents[activeInventoryComponent] == "LED")
				{
					terminals.push_back({ lastTerminalId, GetWorldMouse(), false, "buffer", lastComponentId });
					lastTerminalId++;
				}

				lastComponentId++;
			}
			else
			{
				PlaceModule(inventoryModules[activeInventoryModule]);
			}


			redrawRequired = true;
		}

		if (GetKey(olc::DEL).bReleased)
		{
			deleteClosest();
			redrawRequired = true;
		}


		if (GetKey(olc::S).bReleased)
			Save();

		if (GetKey(olc::L).bReleased)
		{
			Save();
			Load();
			redrawRequired = true;
		}

		if (GetMouse(1).bReleased)
		{
			bool needsNotOut = false;
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
						if (terminal.type == "sourceStart" || terminal.type == "transEmitter" || terminal.type == "transNotOut" || terminal.type == "buffer" || terminal.type == "gatedOut" || terminal.type == "clock" || ("aluOut1" <= terminal.type && terminal.type <= "aluOut8") || ("ramOut1" <= terminal.type && terminal.type <= "ramOut8") || ("bus1" <= terminal.type && terminal.type <= "bus8") || ("counterOut1" <= terminal.type && terminal.type <= "counterOut4") || ("microcounterOut1" <= terminal.type && terminal.type <= "microcounterOut3") || ("IROut1" <= terminal.type && terminal.type <= "IROut4") || ("IRDecodeOut5" <= terminal.type && terminal.type <= "IRDecodeOut8") || terminal.type.find("decoderOut") != std::string::npos || ("flagsRegOut1" <= terminal.type && terminal.type <= "flagsRegOut2") || (terminal.type == "aluZeroFlagOut" || terminal.type == "aluCarryFlagOut"))
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
						if (terminal.type == "transCollector" || terminal.type == "transBase" || terminal.type == "sourceEnd" || terminal.type == "buffer" || terminal.type == "gatedIn" || terminal.type == "gatedWriteEnable" || ("aluInA1" <= terminal.type && terminal.type <= "aluInA8") || ("aluInB1" <= terminal.type && terminal.type <= "aluInB8") || ("ramIn1" <= terminal.type && terminal.type <= "ramIn8") || ("ramAddressIn1" <= terminal.type && terminal.type <= "ramAddressIn4") || terminal.type == "ramWriteEnable" || ("bus1" <= terminal.type && terminal.type <= "bus8") || ("counterIn1" <= terminal.type && terminal.type <= "counterIn4") || terminal.type == "counterClock" || terminal.type == "counterWriteEnable" || terminal.type == "counterCountEnable" || ("microcounterIn1" <= terminal.type && terminal.type <= "microcounterIn3") || ("IRIn1" <= terminal.type && terminal.type <= "IRIn8") || terminal.type == "IRWriteEnable" || ("displayIn1" <= terminal.type && terminal.type <= "displayIn8") || terminal.type == "displayWriteEnable" || ("decoderIn1" <= terminal.type && terminal.type <= "decoderIn9") || terminal.type == "clockHalt" || terminal.type == "aluSub" || ("flagsRegIn1" <= terminal.type && terminal.type <= "flagsRegIn2") || terminal.type == "flagsRegWriteEnable" || terminal.type == "microcounterReset")
						{
							smallestDistance = distance;
							closestTerminalId = terminal.id;
							closestTerminalPos = terminal.pos;
						}

						if (terminal.type == "transCollector")
						{
							needsNotOut = true;
							transistorId = terminal.componentId;
						}
						else
						{
							needsNotOut = false;
							transistorId = 0;
						}
					}
				}


				if (smallestDistance < 10.00 && selectedTerminalA != closestTerminalId)
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
					terminals.push_back({ lastTerminalId, notOutPos, false, "transNotOut", transistorId });
					lastTerminalId++;
				}

				updateSourceConnections();
				updateSimulation = true;

				selectedTerminalA = 0;
				selectedTerminalB = 0;
				selectedTerminalAPos = olc::vi2d(0, 0);
				selectedTerminalBPos = olc::vi2d(0, 0);
			}

			redrawRequired = true;
		}

		if (GetKey(olc::Key::SPACE).bReleased)
		{
			if (activeInventoryComponent < (int)inventoryComponents.size() - 1)
				activeInventoryComponent++;
			else
				activeInventoryComponent = 0;

			redrawRequired = true;
		}

		if (GetKey(olc::Key::A).bReleased)
		{
			if (selectedRamAddress < 15)
				selectedRamAddress++;
			else
				selectedRamAddress = 0;

			updateSimulation = true;
			redrawRequired = true;
		}

		if (GetKey(olc::Key::N).bReleased)
		{
			if (activeInventoryModule < (int)inventoryModules.size() - 1)
				activeInventoryModule++;
			else
				activeInventoryModule = 0;

			redrawRequired = true;
		}

		if (GetKey(olc::Key::M).bReleased)
		{
			if (!placingModule)
				placingModule = true;
			else
				placingModule = false;

			redrawRequired = true;
		}

		if (placingModule)
			redrawRequired = true;

		if (GetKey(olc::Key::B).bReleased)
		{
			if (currentBusColumn < 8)
				currentBusColumn++;
			else
				currentBusColumn = 1;

			redrawRequired = true;
		}

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

			DrawTerminals();
			DrawComponents();
			DrawConnections();
			

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
			redrawRequired = false;
		}

		return !(GetKey(olc::ESCAPE).bPressed);
	}

private:
	olc::panzoom pz;
	bool clockState = false;
	int clockSpeed = 0;
	int clockTicks = 0;
	bool risingEdge = false;
	bool fallingEdge = false;
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
		"DISPLAY",
		"BUFFER",
		"LED",
		"FLAGSREG",
		"TRANSISTOR",
		"GATED LATCH",
		"DECODER",
		"MICROCOUNTER",
		"IR",
		"BUSTERM",
		"ALU",
		"COUNTER",
		"RAM",
	};
	int activeInventoryModule = 0;
	bool placingModule = false;
	std::vector<std::vector<olc::vi2d>> moduleCoordinates = {
		{{-53, -88}, {60, -88}, {-53, 76}, {60, 76}},         // AND
		{{-101, -48}, {97, -48}, {-101, 51}, {97, 51}},       // OR
		{{-53, -90}, {158, -90}, {-53, 76}, {158, 76}},       // NAND
		{{-151, -130}, {241, -130}, {-151, 191}, {241, 191}}, // XOR
		{{-481, -265}, {468, -265}, {-481, 302}, {468, 302}}, // ADDER
		{{-355, -187}, {354, -187}, {-355, 177}, {354, 177}}, // REG
		{{-101, -48}, {97, -48}, {-101, 51}, {97, 51}},       // BITS
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
		"BITS",
		"REGBUS",
		"REGS",
	};
	bool simulationPaused = false;
	bool updateSimulation = false;
	bool componentBuilderMode = false;
	bool redrawRequired = true;
	bool ramFixMode = true;
	bool showInfo = false;
	bool startZooming = false;
	int aluA = 0;
	int aluB = 0;
	int aluO = 0;
	int selectedRamAddress = 0;
	std::vector<std::vector<int>> ramContents = loadProgram("fibonacci");
	int currentBusColumn = 1;
	std::vector<int> IRContents = { 0, 0, 0, 0, 0, 0, 0, 0 };
	std::vector<int> displayContents = { 0, 0, 0, 0, 0, 0, 0, 0 };
	std::vector<int> decoderContents = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	std::vector<int> flagsRegContents = { 0, 0 };
	int counterValue = 0;
	int microcounterValue = 0;
	bool counterCounted = false;
	bool microcounterCounted = false;

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
				false,
				rawTerminalType,
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
		// Full Computer   1634261839
		// Full Demo       1634296642
		// ALU Testing     1634172481
		// RAM Testing     1634283966
		std::string load_timestamp = "1634261839";
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
				false,
				rawTerminalType,
				stoi(rawTerminalComponentId),
				});
		}

		updateSourceConnections();
		updateSimulation = true;
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


				olc::Pixel ledColour = olc::VERY_DARK_RED;

				for (auto terminal : terminals)
				{
					if (terminal.componentId == component.id && terminal.type == "gatedOut" && terminal.state)
					{
						ledColour = olc::RED;
						break;
					}
				}

				DrawLed(componentScreenPos, ledColour);
			}

			if (component.type == "ALU")
			{
				DrawALU(componentScreenPos);
			}

			if (component.type == "RAM")
			{
				DrawRAM(componentScreenPos);
			}

			if (component.type == "COUNTER")
			{
				DrawCounter(componentScreenPos);
			}

			if (component.type == "MICROCOUNTER")
			{
				DrawMicrocounter(componentScreenPos);
			}

			if (component.type == "IR")
			{
				DrawIR(componentScreenPos);
			}

			if (component.type == "DECODER")
			{
				DrawDecoder(componentScreenPos);
			}

			if (component.type == "FLAGSREG")
				DrawFlagsReg(componentScreenPos);

			if (component.type == "DISPLAY")
			{
				DrawDisplay(componentScreenPos);
			}

			if (component.type == "LED")
			{
				olc::Pixel ledColour = olc::VERY_DARK_GREEN;

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

			if ("bus1" <= terminal.type && terminal.type <= "bus8")
			{
				colour = olc::CYAN;

				// Removing the character '0' converts the char '1' to the int 1.
				std::string busColumnStringNumber = std::to_string(terminal.type[3] - '0');

				// DrawString(terminalScreenPos + olc::vi2d(5, 15), busColumnStringNumber, olc::WHITE);
			}

			if (terminal.state)
				colour = olc::GREEN;

			if (selectedTerminalA == terminal.id || selectedTerminalB == terminal.id)
				colour = olc::MAGENTA;

			DrawTerminal(terminalScreenPos, colour);
			// DrawString(terminalScreenPos + olc::vi2d(5, -15), std::to_string(terminal.id), olc::WHITE);
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

		int startY = 110;
		int spacer = 20;

		if (showInfo)
		{
			DrawString(olc::vi2d(50, startY + (spacer * 0)), "FETCH XXXX", olc::CYAN);
			DrawString(olc::vi2d(50, startY + (spacer * 1)), "000 - MI, CO", olc::WHITE);
			DrawString(olc::vi2d(50, startY + (spacer * 2)), "001 - RO, II, CE", olc::WHITE);
			DrawString(olc::vi2d(50, startY + (spacer * 4)), "NOP 0000", olc::CYAN);
			DrawString(olc::vi2d(50, startY + (spacer * 6)), "LDA 0001", olc::CYAN);
			DrawString(olc::vi2d(50, startY + (spacer * 7)), "010 - MI, IO", olc::WHITE);
			DrawString(olc::vi2d(50, startY + (spacer * 8)), "011 - RO, AI", olc::WHITE);
			DrawString(olc::vi2d(50, startY + (spacer * 10)), "ADD 0010", olc::CYAN);
			DrawString(olc::vi2d(50, startY + (spacer * 11)), "010 - MI, IO", olc::WHITE);
			DrawString(olc::vi2d(50, startY + (spacer * 12)), "011 - RO, BI", olc::WHITE);
			DrawString(olc::vi2d(50, startY + (spacer * 13)), "100 - AI, EO, FI", olc::WHITE);
			DrawString(olc::vi2d(50, startY + (spacer * 15)), "SUB 0011", olc::CYAN);
			DrawString(olc::vi2d(50, startY + (spacer * 16)), "010 - MI, IO", olc::WHITE);
			DrawString(olc::vi2d(50, startY + (spacer * 17)), "011 - RO, BI", olc::WHITE);
			DrawString(olc::vi2d(50, startY + (spacer * 18)), "100 - AI, EO, SU, FI", olc::WHITE);
			DrawString(olc::vi2d(50, startY + (spacer * 20)), "STA 0100", olc::CYAN);
			DrawString(olc::vi2d(50, startY + (spacer * 21)), "010 - MI, IO", olc::WHITE);
			DrawString(olc::vi2d(50, startY + (spacer * 22)), "011 - AO, RI", olc::WHITE);
			DrawString(olc::vi2d(50, startY + (spacer * 24)), "LDI 0101", olc::CYAN);
			DrawString(olc::vi2d(50, startY + (spacer * 25)), "010 - IO, AI", olc::WHITE);
			DrawString(olc::vi2d(50, startY + (spacer * 27)), "JMP 0110", olc::CYAN);
			DrawString(olc::vi2d(50, startY + (spacer * 28)), "010 - IO, JU", olc::WHITE);
			DrawString(olc::vi2d(50, startY + (spacer * 30)), "JC  0111", olc::CYAN);
			DrawString(olc::vi2d(50, startY + (spacer * 31)), "010 - IO, JU", olc::WHITE);
			DrawString(olc::vi2d(50, startY + (spacer * 33)), "JZ  1000", olc::CYAN);
			DrawString(olc::vi2d(50, startY + (spacer * 34)), "010 - IO, JU", olc::WHITE);
			DrawString(olc::vi2d(50, startY + (spacer * 36)), "OUT 1110", olc::CYAN);
			DrawString(olc::vi2d(50, startY + (spacer * 37)), "010 - AO, OI", olc::WHITE);
			DrawString(olc::vi2d(50, startY + (spacer * 39)), "HLT 1111", olc::CYAN);
			DrawString(olc::vi2d(50, startY + (spacer * 40)), "010 - HT", olc::WHITE);
		}
			
	}


	void DrawTransistor(olc::vi2d pos)
	{
		int size = 25 * pz.GetScale().x;
		DrawLine(pos + olc::vi2d(0, -size), pos + olc::vi2d(0, size), olc::GREEN);
		DrawLine(pos, pos + olc::vi2d(-size, 0), olc::GREEN);
		DrawLine(pos + olc::vi2d(0, -size / 2), pos + olc::vi2d(size, -size), olc::GREEN);
		DrawLine(pos + olc::vi2d(0, size / 2), pos + olc::vi2d(size, size), olc::GREEN);
	}

	void DrawGatedLatch(olc::vi2d pos)
	{
		int squareWidth = 50 * pz.GetScale().x;
		DrawRect(pos + olc::vi2d(-squareWidth / 2, -squareWidth / 2), { squareWidth, squareWidth }, olc::RED);
	}

	void DrawALU(olc::vi2d pos)
	{
		float scale = pz.GetScale().x;
		int squareWidth = 300 * scale;
		DrawRect(pos, { squareWidth, squareWidth }, olc::WHITE);

		if (scale > 0.6)
			DrawString(pos + olc::vf2d(squareWidth / 2 - 40, squareWidth / 2), "VISCOM ALU", olc::DARK_GREY);

		DrawString(pos + olc::vf2d(squareWidth / 2, squareWidth / 8), std::to_string(aluA), olc::GREEN);
		DrawString(pos + olc::vf2d(squareWidth / 2, squareWidth / 8 * 7), std::to_string(aluB), olc::GREEN);
		DrawString(pos + olc::vf2d(squareWidth / 8, squareWidth / 2), std::to_string(aluO), olc::GREEN);
	}

	void DrawRAM(olc::vi2d pos)
	{
		float scale = pz.GetScale().x;
		int ramWidth = 215 * scale;
		int ramHeight = 415 * scale;
		float bitPadding = 25 * scale;
		float ramToBitsPadding = 20 * scale;
		DrawRect(pos, { ramWidth, ramHeight }, olc::WHITE);
		for (int ramAddress = 0; ramAddress < ramContents.size(); ramAddress++)
		{
			for (int ramBit = 0; ramBit < ramContents[0].size(); ramBit++)
			{
				int thisBit = ramContents[ramAddress][ramBit];
				olc::Pixel ramBitColor = olc::VERY_DARK_RED;

				if (thisBit)
				{
					ramBitColor = olc::RED;
				}

				DrawLed(pos + olc::vf2d(ramToBitsPadding + ramBit * bitPadding, ramToBitsPadding + ramAddress * bitPadding), ramBitColor);
			}
		}

		// Draw selected address indicator
		float addressPadding = ramToBitsPadding;
		float intraAddressPadding = (addressPadding + (5 * scale)) * selectedRamAddress;
		float fullPadding = addressPadding + intraAddressPadding;
		DrawLine(pos + olc::vf2d(0, fullPadding), pos + olc::vf2d(ramWidth, fullPadding), olc::MAGENTA);		
	}

	void DrawCounter(olc::vi2d pos)
	{
		float scale = pz.GetScale().x;
		int counterWidth = 120 * scale;
		int counterHeight = 50 * scale;
		float bitPadding = 25 * scale;

		DrawRect(pos, { counterWidth, counterHeight }, olc::WHITE);

		std::string counterValueBinary = decimalToBinaryString(counterValue, 4);

		olc::Pixel ledColour = olc::VERY_DARK_GREEN;

		if (counterValueBinary[0] == '1')
			ledColour = olc::GREEN;
		else
			ledColour = olc::VERY_DARK_GREEN;

		DrawLed(pos + olc::vf2d(bitPadding - 4, bitPadding), ledColour);

		if (counterValueBinary[1] == '1')
			ledColour = olc::GREEN;
		else
			ledColour = olc::VERY_DARK_GREEN;

		DrawLed(pos + olc::vf2d(bitPadding * 2 - 4, bitPadding), ledColour);

		if (counterValueBinary[2] == '1')
			ledColour = olc::GREEN;
		else
			ledColour = olc::VERY_DARK_GREEN;

		DrawLed(pos + olc::vf2d(bitPadding * 3 - 4, bitPadding), ledColour);

		if (counterValueBinary[3] == '1')
			ledColour = olc::GREEN;
		else
			ledColour = olc::VERY_DARK_GREEN;

		DrawLed(pos + olc::vf2d(bitPadding * 4 - 4, bitPadding), ledColour);
	}

	void DrawMicrocounter(olc::vi2d pos)
	{
		float scale = pz.GetScale().x;
		int counterWidth = 90 * scale;
		int counterHeight = 50 * scale;
		float bitPadding = 25 * scale;

		DrawRect(pos, { counterWidth, counterHeight }, olc::WHITE);

		std::string counterValueBinary = decimalToBinaryString(microcounterValue, 3);

		olc::Pixel ledColour = olc::VERY_DARK_RED;

		if (counterValueBinary[0] == '1')
			ledColour = olc::RED;
		else
			ledColour = olc::VERY_DARK_RED;

		DrawLed(pos + olc::vf2d(bitPadding - 4, bitPadding), ledColour);

		if (counterValueBinary[1] == '1')
			ledColour = olc::RED;
		else
			ledColour = olc::VERY_DARK_RED;

		DrawLed(pos + olc::vf2d(bitPadding * 2 - 4, bitPadding), ledColour);

		if (counterValueBinary[2] == '1')
			ledColour = olc::RED;
		else
			ledColour = olc::VERY_DARK_RED;

		DrawLed(pos + olc::vf2d(bitPadding * 3 - 4, bitPadding), ledColour);
	}

	void DrawIR(olc::vi2d pos)
	{
		float scale = pz.GetScale().x;
		int width = 240 * scale;
		int height = 50 * scale;
		float bitPadding = 25 * scale;

		DrawRect(pos, { width, height }, olc::WHITE);

		olc::Pixel ledColour = olc::WHITE;

		for (int bit = 0; bit < IRContents.size(); bit++)
		{
			bool thisState = IRContents[bit];
			bool leastSignificant = bit <= 3;
			float xOffset = 34 * scale;
		
			if (leastSignificant)
			{
				if (thisState)
					ledColour = olc::YELLOW;
				else
					ledColour = olc::VERY_DARK_YELLOW;
			}	
			else
			{
				if (thisState)
					ledColour = olc::CYAN;
				else
					ledColour = olc::VERY_DARK_CYAN;
			}

			DrawLed(pos + olc::vf2d(bitPadding * -bit - xOffset + width, bitPadding), ledColour);
		}

	}

	void DrawDecoder(olc::vi2d pos)
	{
		float scale = pz.GetScale().x;
		int width = 450 * scale;
		int height = 60 * scale;
		float bitPadding = 25 * scale;

		DrawRect(pos, { width, height }, olc::WHITE);

		olc::Pixel ledColour = olc::VERY_DARK_BLUE;

		std::vector<std::string> controlLabels = {"HT", "MI", "RI", "RO" , "IO" , "II" , "AI" , "AO" , "EO" , "SU" , "BI" , "OI" , "CE" , "CO" , "JU", "FI", "MR" };

		for (int bit = 0; bit < decoderContents.size(); bit++)
		{
			bool thisState = decoderContents[bit];
			float xOffset = 25 * scale;


			if (thisState)
				ledColour = olc::CYAN;
			else
				ledColour = olc::VERY_DARK_CYAN;

			if (thisState)
				ledColour = olc::CYAN;
			else
				ledColour = olc::VERY_DARK_CYAN;

			DrawLed(pos + olc::vf2d(bitPadding * bit + xOffset, bitPadding), ledColour);

			if (scale >= 1)
				DrawString(pos + olc::vf2d(bitPadding * bit + xOffset - (7 * scale), bitPadding + (16 * scale)), controlLabels[bit], olc::WHITE, scale);
			else
				DrawCircle(pos + olc::vf2d(bitPadding * bit + xOffset, bitPadding + (20 * scale)), 2 * scale, olc::WHITE);
		}
	}

	void DrawFlagsReg(olc::vi2d pos)
	{
		float scale = pz.GetScale().x;
		int width = 75 * scale;
		int height = 60 * scale;
		float bitPadding = 25 * scale;

		DrawRect(pos, { width, height }, olc::WHITE);

		olc::Pixel ledColour = olc::WHITE;

		std::vector<std::string> labels = { "CF", "ZF" };

		for (int bit = 0; bit < flagsRegContents.size(); bit++)
		{
			bool thisState = flagsRegContents[bit];
			float xOffset = 25 * scale;


			if (thisState)
				ledColour = olc::GREEN;
			else
				ledColour = olc::VERY_DARK_GREEN;

			if (thisState)
				ledColour = olc::GREEN;
			else
				ledColour = olc::VERY_DARK_GREEN;

			DrawLed(pos + olc::vf2d(bitPadding * bit + xOffset, bitPadding), ledColour);

			if (scale >= 1)
				DrawString(pos + olc::vf2d(bitPadding * bit + xOffset - (7 * scale), bitPadding + (16 * scale)), labels[bit], olc::WHITE, scale);
			else
				DrawCircle(pos + olc::vf2d(bitPadding * bit + xOffset, bitPadding + (20 * scale)), 2 * scale, olc::WHITE);
		}
	}

	bool DecodeDisplayPixel(char digit, int pixelX, int pixelY)
	{
		std::vector<std::vector<int>> zero = {
			{ 0, 1, 1, 0 },
			{ 1, 0, 0, 1 },
			{ 1, 0, 0, 1 },
			{ 1, 0, 0, 1 },
			{ 0, 1, 1, 0 },
		};

		std::vector<std::vector<int>> one = {
			{ 0, 1, 0, 0 },
			{ 1, 1, 0, 0 },
			{ 0, 1, 0, 0 },
			{ 0, 1, 0, 0 },
			{ 1, 1, 1, 0 },
		};

		std::vector<std::vector<int>> two = {
			{ 1, 1, 0, 0 },
			{ 0, 0, 1, 0 },
			{ 0, 1, 1, 0 },
			{ 1, 0, 0, 0 },
			{ 1, 1, 1, 0 },
		};

		std::vector<std::vector<int>> three = {
			{ 1, 1, 0, 0 },
			{ 0, 0, 1, 0 },
			{ 1, 1, 0, 0 },
			{ 0, 0, 1, 0 },
			{ 1, 1, 0, 0 },
		};

		std::vector<std::vector<int>> four = {
			{ 0, 0, 1, 0 },
			{ 0, 1, 1, 0 },
			{ 1, 0, 1, 0 },
			{ 1, 1, 1, 1 },
			{ 0, 0, 1, 0 },
		};

		std::vector<std::vector<int>> five = {
			{ 1, 1, 1, 0 },
			{ 1, 0, 0, 0 },
			{ 1, 1, 0, 0 },
			{ 0, 0, 1, 0 },
			{ 1, 1, 0, 0 },
		};

		std::vector<std::vector<int>> six = {
			{ 0, 1, 1, 0 },
			{ 1, 0, 0, 0 },
			{ 1, 1, 1, 0 },
			{ 1, 0, 0, 1 },
			{ 0, 1, 1, 0 },
		};

		std::vector<std::vector<int>> seven = {
			{ 1, 1, 1, 0 },
			{ 1, 0, 1, 0 },
			{ 0, 0, 1, 0 },
			{ 0, 0, 1, 0 },
			{ 0, 0, 1, 0 },
		};

		std::vector<std::vector<int>> eight = {
			{ 0, 1, 1, 0 },
			{ 1, 0, 0, 1 },
			{ 0, 1, 1, 0 },
			{ 1, 0, 0, 1 },
			{ 0, 1, 1, 0 },
		};

		std::vector<std::vector<int>> nine = {
			{ 0, 1, 1, 0 },
			{ 1, 0, 0, 1 },
			{ 0, 1, 1, 1 },
			{ 0, 0, 0, 1 },
			{ 0, 1, 1, 0 },
		};

		if (digit == '0')
			return zero[pixelY][pixelX];
		if (digit == '1')
			return one[pixelY][pixelX];
		if (digit == '2')
			return two[pixelY][pixelX];
		if (digit == '3')
			return three[pixelY][pixelX];
		if (digit == '4')
			return four[pixelY][pixelX];
		if (digit == '5')
			return five[pixelY][pixelX];
		if (digit == '6')
			return six[pixelY][pixelX];
		if (digit == '7')
			return seven[pixelY][pixelX];
		if (digit == '8')
			return eight[pixelY][pixelX];
		if (digit == '9')
			return nine[pixelY][pixelX];


		return 0;
	}

	void DrawDisplay(olc::vi2d pos)
	{
		float scale = pz.GetScale().x;
		int width = 405 * scale;
		int height = 180 * scale;

		DrawRect(pos, { width, height }, olc::WHITE);

		int displayDecimal = 0;
		displayDecimal += displayContents[0] * 128;
		displayDecimal += displayContents[1] * 64;
		displayDecimal += displayContents[2] * 32;
		displayDecimal += displayContents[3] * 16;
		displayDecimal += displayContents[4] * 8;
		displayDecimal += displayContents[5] * 4;
		displayDecimal += displayContents[6] * 2;
		displayDecimal += displayContents[7] * 1;

		std::string displayDecimalString = std::to_string(displayDecimal);
		std::string displayChar1 = "0";
		std::string displayChar2 = "0";
		std::string displayChar3 = "0";

		if (displayDecimalString.size() == 3)
		{
			displayChar1 = displayDecimalString[0];
			displayChar2 = displayDecimalString[1];
			displayChar3 = displayDecimalString[2];
		}
		else if (displayDecimalString.size() == 2)
		{
			displayChar2 = displayDecimalString[0];
			displayChar3 = displayDecimalString[1];
		}
		else if (displayDecimalString.size() == 1)
		{
			displayChar3 = displayDecimalString[0];
		}

		std::string combinedPaddedString = displayChar1 + displayChar2 + displayChar3;

		int digitPixelWidth = 4;
		int digitPixelHeight = 5;
		float intradigitSpacing = 30 * scale;
		float interdigitSpacing = 32 * (digitPixelWidth) * scale;
		float displayPadding = 30 * scale;

		for (int thisDigitIterator = 0; thisDigitIterator < combinedPaddedString.size(); thisDigitIterator++)
		{
			for (int thisX = 0; thisX < digitPixelWidth; thisX++)
			{
				for (int thisY = 0; thisY < digitPixelHeight; thisY++)
				{
					char thisDigit = combinedPaddedString[thisDigitIterator];
					float ledDigitXOffset = (thisDigitIterator * interdigitSpacing);
					float ledXOffset = thisX * intradigitSpacing + displayPadding;
					float ledYOffset = thisY * intradigitSpacing + displayPadding;
					olc::Pixel ledColour = olc::VERY_DARK_RED;

					if (DecodeDisplayPixel(thisDigit, thisX, thisY))
						ledColour = olc::RED;

					DrawLed(pos + olc::vf2d(ledDigitXOffset + ledXOffset, ledYOffset), ledColour);
				}
			}
		}

		//DrawString(pos + olc::vf2d{float(width / 7), float(height / 3)}, combinedPaddedString, olc::RED, float(4 * 2.5 * scale));
	}

	void DrawSource(olc::vi2d pos)
	{
		float scale = pz.GetScale().x;
		DrawLine(pos + olc::vi2d(-25 * scale, 0), pos + olc::vi2d(-5 * scale, 0), olc::GREY);
		DrawLine(pos + olc::vi2d(-5 * scale, -20 * scale), pos + olc::vi2d(-5 * scale, 20 * scale), olc::GREY);
		DrawLine(pos + olc::vi2d(5 * scale, -30 * scale), pos + olc::vi2d(5 * scale, 30 * scale), olc::GREEN);
		DrawLine(pos + olc::vi2d(5 * scale, 0), pos + olc::vi2d(25 * scale, 0), olc::GREEN);
	}

	void DrawClock(olc::vi2d pos)
	{
		float scale = pz.GetScale().x;
		olc::Pixel clockColour = olc::GREY;
		if (clockState)
			clockColour = olc::GREEN;

		float xOffset;
		if (clockSpeed > 9)
			xOffset = -6 * scale;
		else
			xOffset = -3 * scale;

		DrawCircle(pos, 30 * scale, clockColour);
		DrawString(pos + olc::vi2d(xOffset, 15 * scale), std::to_string(clockSpeed), clockColour);
	}

	void DrawTerminal(olc::vi2d pos, olc::Pixel colour)
	{
		float scale = pz.GetScale().x;
		float radius = 3;

		if (scale < 0.5)
			radius = 2;

		DrawCircle(pos, radius, colour);
	}

	void DrawLed(olc::vi2d pos, olc::Pixel colour)
	{
		for (int i = 1; i < (10 * pz.GetScale().x); i++)
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

	Terminal* findTerminalByComponent(int componentId, std::string terminalType)
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
		bool baseState = false;
		bool collectorState = false;
		bool baseFound = false;
		bool collectorFound = false;
		Terminal* emitterTerminal = nullptr;
		Terminal* notOutTerminal = nullptr;

		for (auto& terminal : terminals)
		{
			if (terminal.componentId == id && terminal.type == "transCollector")
			{
				collectorFound = true;

				if (terminal.state)
					collectorState = true;
			}

			if (terminal.componentId == id && terminal.type == "transBase")
			{
				baseFound = true;

				if (terminal.state)
					baseState = true;
			}

			if (terminal.componentId == id && terminal.type == "transEmitter")
				emitterTerminal = &terminal;

			if (terminal.componentId == id && terminal.type == "transNotOut")
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
		bool dataInState = false;
		bool writeEnableState = false;
		bool dataInFound = false;
		bool writeEnableFound = false;

		Terminal* dataOutTerminal = nullptr;

		for (auto& terminal : terminals)
		{
			if (terminal.componentId == id && terminal.type == "gatedIn")
			{
				dataInFound = true;

				if (terminal.state)
					dataInState = true;
			}

			if (terminal.componentId == id && terminal.type == "gatedWriteEnable")
			{
				writeEnableFound = true;

				if (terminal.state)
					writeEnableState = true;
			}

			if (terminal.componentId == id && terminal.type == "gatedOut")
				dataOutTerminal = &terminal;

			if (dataInFound && writeEnableFound && dataOutTerminal)
				break;
		}

		if (dataOutTerminal && writeEnableState && risingEdge)
		{
			dataOutTerminal->state = dataInState;
		}
	}

	std::string decimalToBinaryString(int decimalInput, int numBits)
	{
		int cumulativeResult = decimalInput;
		std::vector<int> powers = { };
		
		if (numBits == 8)
			powers = { 128, 64, 32, 16, 8, 4, 2, 1 };
		else if (numBits == 4)
			powers = { 8, 4, 2, 1 };
		else if (numBits == 3)
			powers = { 4, 2, 1 };

		std::string outputBinary = "";
		for (auto power : powers)
		{
			if (cumulativeResult >= power)
			{
				outputBinary += "1";
				cumulativeResult -= power;
			}
			else
			{
				outputBinary += "0";
			}
		}

		return outputBinary;
	}

	void simulateALU()
	{
		int a = 0;
		int b = 0;

		Terminal* subBit = NULL;

		Terminal* outputBit8 = NULL;
		Terminal* outputBit7 = NULL;
		Terminal* outputBit6 = NULL;
		Terminal* outputBit5 = NULL;
		Terminal* outputBit4 = NULL;
		Terminal* outputBit3 = NULL;
		Terminal* outputBit2 = NULL;
		Terminal* outputBit1 = NULL;

		Terminal* zeroFlagOut = NULL;
		Terminal* carryFlagOut = NULL;

		for (auto& terminal : terminals)
		{
			if (terminal.state)
			{
				if (terminal.type == "aluInA1")
					a += 128;
				else if (terminal.type == "aluInA2")
					a += 64;
				else if (terminal.type == "aluInA3")
					a += 32;
				else if (terminal.type == "aluInA4")
					a += 16;
				else if (terminal.type == "aluInA5")
					a += 8;
				else if (terminal.type == "aluInA6")
					a += 4;
				else if (terminal.type == "aluInA7")
					a += 2;
				else if (terminal.type == "aluInA8")
					a += 1;
				else if (terminal.type == "aluInB1")
					b += 128;
				else if (terminal.type == "aluInB2")
					b += 64;
				else if (terminal.type == "aluInB3")
					b += 32;
				else if (terminal.type == "aluInB4")
					b += 16;
				else if (terminal.type == "aluInB5")
					b += 8;
				else if (terminal.type == "aluInB6")
					b += 4;
				else if (terminal.type == "aluInB7")
					b += 2;
				else if (terminal.type == "aluInB8")
					b += 1;
			}
			
			if (terminal.type == "aluOut1")
				outputBit1 = &terminal;
			else if (terminal.type == "aluOut2")
				outputBit2 = &terminal;
			else if (terminal.type == "aluOut3")
				outputBit3 = &terminal;
			else if (terminal.type == "aluOut4")
				outputBit4 = &terminal;
			else if (terminal.type == "aluOut5")
				outputBit5 = &terminal;
			else if (terminal.type == "aluOut6")
				outputBit6 = &terminal;
			else if (terminal.type == "aluOut7")
				outputBit7 = &terminal;
			else if (terminal.type == "aluOut8")
				outputBit8 = &terminal;
			else if (terminal.type == "aluSub")
				subBit = &terminal;
			else if (terminal.type == "aluZeroFlagOut")
				zeroFlagOut = &terminal;
			else if (terminal.type == "aluCarryFlagOut")
				carryFlagOut = &terminal;
		}

		aluA = a;
		aluB = b;

		if (subBit && subBit->state)
			{
				aluO = a - b;
			}
		else 
			aluO = a + b;

		if (carryFlagOut)
		{
			if (aluO > 255)
			{
				aluO = 255;
				carryFlagOut->state = true;
			}
			else
			{
				if (subBit && subBit->state && b == 1 && a != 0)
					carryFlagOut->state = true;
				else
					carryFlagOut->state = false;
			}
			
		}

		bool isNegative = false;
		int aluOTwosComplement = 0;
			

		if (aluO < 0)
		{
			isNegative = true;
			aluOTwosComplement = -aluO - 1;
		}
			

		if (zeroFlagOut)
			zeroFlagOut->state = aluO == 0;
			

		std::string outputBinary = decimalToBinaryString(aluO, 8);

		if (isNegative)
			outputBinary = decimalToBinaryString(aluOTwosComplement, 8);


		if (outputBit1 && outputBit2 && outputBit3 && outputBit4 && outputBit5 && outputBit6 && outputBit7 && outputBit8)
		{
			outputBit1->state = outputBinary[0] == '1';
			outputBit2->state = outputBinary[1] == '1';
			outputBit3->state = outputBinary[2] == '1';
			outputBit4->state = outputBinary[3] == '1';
			outputBit5->state = outputBinary[4] == '1';
			outputBit6->state = outputBinary[5] == '1';
			outputBit7->state = outputBinary[6] == '1';
			outputBit8->state = outputBinary[7] == '1';

			if (isNegative)
			{
				outputBit1->state = !outputBit1->state;
				outputBit2->state = !outputBit2->state;
				outputBit3->state = !outputBit3->state;
				outputBit4->state = !outputBit4->state;
				outputBit5->state = !outputBit5->state;
				outputBit6->state = !outputBit6->state;
				outputBit7->state = !outputBit7->state;
				outputBit8->state = !outputBit8->state;
			}
		}
	}

	void simulateALU_NEW()
	{
		std::cout << "Simulating ALU..." << std::endl;
		
		Terminal* subBit = NULL;

		Terminal* outputBit8 = NULL;
		Terminal* outputBit7 = NULL;
		Terminal* outputBit6 = NULL;
		Terminal* outputBit5 = NULL;
		Terminal* outputBit4 = NULL;
		Terminal* outputBit3 = NULL;
		Terminal* outputBit2 = NULL;
		Terminal* outputBit1 = NULL;

		Terminal* inputBitA1 = NULL;
		Terminal* inputBitA2 = NULL;
		Terminal* inputBitA3 = NULL;
		Terminal* inputBitA4 = NULL;
		Terminal* inputBitA5 = NULL;
		Terminal* inputBitA6 = NULL;
		Terminal* inputBitA7 = NULL;
		Terminal* inputBitA8 = NULL;

		Terminal* inputBitB1 = NULL;
		Terminal* inputBitB2 = NULL;
		Terminal* inputBitB3 = NULL;
		Terminal* inputBitB4 = NULL;
		Terminal* inputBitB5 = NULL;
		Terminal* inputBitB6 = NULL;
		Terminal* inputBitB7 = NULL;
		Terminal* inputBitB8 = NULL;

		Terminal* zeroFlagOut = NULL;
		Terminal* carryFlagOut = NULL;

		for (auto& terminal : terminals)
		{
			if (terminal.type == "aluInA1")
				inputBitA1 = &terminal;
			else if (terminal.type == "aluInA2")
				inputBitA2 = &terminal;
			else if (terminal.type == "aluInA3")
				inputBitA3 = &terminal;
			else if (terminal.type == "aluInA4")
				inputBitA4 = &terminal;
			else if (terminal.type == "aluInA5")
				inputBitA5 = &terminal;
			else if (terminal.type == "aluInA6")
				inputBitA6 = &terminal;
			else if (terminal.type == "aluInA7")
				inputBitA7 = &terminal;
			else if (terminal.type == "aluInA8")
				inputBitA8 = &terminal;
			else if (terminal.type == "aluInB1")
				inputBitB1 = &terminal;
			else if (terminal.type == "aluInB2")
				inputBitB2 = &terminal;
			else if (terminal.type == "aluInB3")
				inputBitB3 = &terminal;
			else if (terminal.type == "aluInB4")
				inputBitB4 = &terminal;
			else if (terminal.type == "aluInB5")
				inputBitB5 = &terminal;
			else if (terminal.type == "aluInB6")
				inputBitB6 = &terminal;
			else if (terminal.type == "aluInB7")
				inputBitB7 = &terminal;
			else if (terminal.type == "aluInB8")
				inputBitB8 = &terminal;
			else if (terminal.type == "aluOut1")
				outputBit1 = &terminal;
			else if (terminal.type == "aluOut2")
				outputBit2 = &terminal;
			else if (terminal.type == "aluOut3")
				outputBit3 = &terminal;
			else if (terminal.type == "aluOut4")
				outputBit4 = &terminal;
			else if (terminal.type == "aluOut5")
				outputBit5 = &terminal;
			else if (terminal.type == "aluOut6")
				outputBit6 = &terminal;
			else if (terminal.type == "aluOut7")
				outputBit7 = &terminal;
			else if (terminal.type == "aluOut8")
				outputBit8 = &terminal;
			else if (terminal.type == "aluSub")
				subBit = &terminal;
			else if (terminal.type == "aluZeroFlagOut")
				zeroFlagOut = &terminal;
			else if (terminal.type == "aluCarryFlagOut")
				carryFlagOut = &terminal;
		}

		aluA = 0;
		aluB = 0;

		std::string aBinaryString = "";
		std::string bBinaryString = "";

		if (inputBitA1 && inputBitA2 && inputBitA3 && inputBitA4 && inputBitA5 && inputBitA6 && inputBitA7 && inputBitA8 && inputBitB1 && inputBitB2 && inputBitB3 && inputBitB4 && inputBitB5 && inputBitB6 && inputBitB7 && inputBitB8)
		{
			aBinaryString += std::to_string(inputBitA1->state);
			aBinaryString += std::to_string(inputBitA2->state);
			aBinaryString += std::to_string(inputBitA3->state);
			aBinaryString += std::to_string(inputBitA4->state);
			aBinaryString += std::to_string(inputBitA5->state);
			aBinaryString += std::to_string(inputBitA6->state);
			aBinaryString += std::to_string(inputBitA7->state);
			aBinaryString += std::to_string(inputBitA8->state);

			bBinaryString += std::to_string(inputBitB1->state);
			bBinaryString += std::to_string(inputBitB2->state);
			bBinaryString += std::to_string(inputBitB3->state);
			bBinaryString += std::to_string(inputBitB4->state);
			bBinaryString += std::to_string(inputBitB5->state);
			bBinaryString += std::to_string(inputBitB6->state);
			bBinaryString += std::to_string(inputBitB7->state);
			bBinaryString += std::to_string(inputBitB8->state);

			aluA += 128 * inputBitA1->state;
			aluA += 64 * inputBitA2->state;
			aluA += 32 * inputBitA3->state;
			aluA += 16 * inputBitA4->state;
			aluA += 8 * inputBitA5->state;
			aluA += 4 * inputBitA6->state;
			aluA += 2 * inputBitA7->state;
			aluA += 1 * inputBitA8->state;
		}

		if (subBit && subBit->state)
		{
			for (char& bit : bBinaryString)
			{
				if (bit == '0')
					bit = '1';
				else if (bit == '1')
					bit = '0';
			}
		}
			

		aluB += 128 * (bBinaryString[0] == '1');
		aluB += 64 * (bBinaryString[1] == '1');
		aluB += 32 * (bBinaryString[2] == '1');
		aluB += 16 * (bBinaryString[3] == '1');
		aluB += 8 * (bBinaryString[4] == '1');
		aluB += 4 * (bBinaryString[5] == '1');
		aluB += 2 * (bBinaryString[6] == '1');
		aluB += 1 * (bBinaryString[7] == '1');

		if (subBit && subBit->state)
			aluB += 1;

		if (aluB > 255)
			aluB = 0;

		if (subBit)
			if (!subBit->state)
				aluO = aluA + aluB;
			else
				aluO = aluA - aluB;

		if (carryFlagOut)
		{
			if (aluO > 255)
			{
				carryFlagOut->state = true;
			}
			else
			{
				carryFlagOut->state = false;
			}

		}

		bool isNegative = false;
		int aluOTwosComplement = 0;

		if (aluO < 0)
		{
			isNegative = true;
			aluOTwosComplement = -aluO - 1;
		}

		if (zeroFlagOut)
			zeroFlagOut->state = aluO == 0;

		std::string outputBinary = decimalToBinaryString(aluO, 8);

		if (isNegative)
			outputBinary = decimalToBinaryString(aluOTwosComplement, 8);


		if (outputBit1 && outputBit2 && outputBit3 && outputBit4 && outputBit5 && outputBit6 && outputBit7 && outputBit8)
		{
			outputBit1->state = outputBinary[0] == '1';
			outputBit2->state = outputBinary[1] == '1';
			outputBit3->state = outputBinary[2] == '1';
			outputBit4->state = outputBinary[3] == '1';
			outputBit5->state = outputBinary[4] == '1';
			outputBit6->state = outputBinary[5] == '1';
			outputBit7->state = outputBinary[6] == '1';
			outputBit8->state = outputBinary[7] == '1';

			if (isNegative)
			{
				outputBit1->state = !outputBit1->state;
				outputBit2->state = !outputBit2->state;
				outputBit3->state = !outputBit3->state;
				outputBit4->state = !outputBit4->state;
				outputBit5->state = !outputBit5->state;
				outputBit6->state = !outputBit6->state;
				outputBit7->state = !outputBit7->state;
				outputBit8->state = !outputBit8->state;
			}
		}
	}

	void simulateRAM()
	{
		Terminal* inputBit8 = NULL;
		Terminal* inputBit7 = NULL;
		Terminal* inputBit6 = NULL;
		Terminal* inputBit5 = NULL;
		Terminal* inputBit4 = NULL;
		Terminal* inputBit3 = NULL;
		Terminal* inputBit2 = NULL;
		Terminal* inputBit1 = NULL;

		Terminal* outputBit8 = NULL;
		Terminal* outputBit7 = NULL;
		Terminal* outputBit6 = NULL;
		Terminal* outputBit5 = NULL;
		Terminal* outputBit4 = NULL;
		Terminal* outputBit3 = NULL;
		Terminal* outputBit2 = NULL;
		Terminal* outputBit1 = NULL;

		Terminal* writeEnableTerminal = NULL;

		Terminal* addressTerminal1 = NULL;
		Terminal* addressTerminal2 = NULL;
		Terminal* addressTerminal3 = NULL;
		Terminal* addressTerminal4 = NULL;

		for (auto& terminal : terminals)
		{
			if (terminal.type == "ramWriteEnable")
				writeEnableTerminal = &terminal;
			else if (terminal.type == "ramOut1")
				outputBit1 = &terminal;
			else if (terminal.type == "ramOut2")
				outputBit2 = &terminal;
			else if (terminal.type == "ramOut3")
				outputBit3 = &terminal;
			else if (terminal.type == "ramOut4")
				outputBit4 = &terminal;
			else if (terminal.type == "ramOut5")
				outputBit5 = &terminal;
			else if (terminal.type == "ramOut6")
				outputBit6 = &terminal;
			else if (terminal.type == "ramOut7")
				outputBit7 = &terminal;
			else if (terminal.type == "ramOut8")
				outputBit8 = &terminal;
			else if (terminal.type == "ramIn1")
				inputBit1 = &terminal;
			else if (terminal.type == "ramIn2")
				inputBit2 = &terminal;
			else if (terminal.type == "ramIn3")
				inputBit3 = &terminal;
			else if (terminal.type == "ramIn4")
				inputBit4 = &terminal;
			else if (terminal.type == "ramIn5")
				inputBit5 = &terminal;
			else if (terminal.type == "ramIn6")
				inputBit6 = &terminal;
			else if (terminal.type == "ramIn7")
				inputBit7 = &terminal;
			else if (terminal.type == "ramIn8")
				inputBit8 = &terminal;
			else if (terminal.type == "ramAddressIn1")
				addressTerminal1 = &terminal;
			else if (terminal.type == "ramAddressIn2")
				addressTerminal2 = &terminal;
			else if (terminal.type == "ramAddressIn3")
				addressTerminal3 = &terminal;
			else if (terminal.type == "ramAddressIn4")
				addressTerminal4 = &terminal;
		}

		if (addressTerminal1 && addressTerminal2 && addressTerminal3 && addressTerminal4)
		{
			int ramAddressSum = 0;
			if (addressTerminal1->state)
				ramAddressSum += 1;

			if (addressTerminal2->state)
				ramAddressSum += 2;

			if (addressTerminal3->state)
				ramAddressSum += 4;

			if (addressTerminal4->state)
				ramAddressSum += 8;

			if (ramAddressSum > 15)
				ramAddressSum = 0;

			selectedRamAddress = ramAddressSum;
		}

		if (writeEnableTerminal && writeEnableTerminal->state)
		{
			if (inputBit1 && inputBit2 && inputBit3 && inputBit4 && inputBit5 && inputBit6 && inputBit7 && inputBit8)
			{
				if (!ramFixMode || (selectedRamAddress > 0 && (inputBit1->state || inputBit2->state || inputBit3->state || inputBit4->state || inputBit5->state || inputBit6->state || inputBit7->state || inputBit8->state)))
				{
					ramContents[selectedRamAddress][0] = inputBit1->state;
					ramContents[selectedRamAddress][1] = inputBit2->state;
					ramContents[selectedRamAddress][2] = inputBit3->state;
					ramContents[selectedRamAddress][3] = inputBit4->state;
					ramContents[selectedRamAddress][4] = inputBit5->state;
					ramContents[selectedRamAddress][5] = inputBit6->state;
					ramContents[selectedRamAddress][6] = inputBit7->state;
					ramContents[selectedRamAddress][7] = inputBit8->state;
				}
			}
		}

		if (outputBit8)
			outputBit8->state = ramContents[selectedRamAddress][7];

		if (outputBit7)
			outputBit7->state = ramContents[selectedRamAddress][6];

		if (outputBit6)
			outputBit6->state = ramContents[selectedRamAddress][5];

		if (outputBit5)
			outputBit5->state = ramContents[selectedRamAddress][4];

		if (outputBit4)
			outputBit4->state = ramContents[selectedRamAddress][3];

		if (outputBit3)
			outputBit3->state = ramContents[selectedRamAddress][2];

		if (outputBit2)
			outputBit2->state = ramContents[selectedRamAddress][1];

		if (outputBit1)
			outputBit1->state = ramContents[selectedRamAddress][0];
	}

	void simulateCounter()
	{
		Terminal* inputBit4 = NULL;
		Terminal* inputBit3 = NULL;
		Terminal* inputBit2 = NULL;
		Terminal* inputBit1 = NULL;

		Terminal* outputBit4 = NULL;
		Terminal* outputBit3 = NULL;
		Terminal* outputBit2 = NULL;
		Terminal* outputBit1 = NULL;

		Terminal* writeEnableTerminal = NULL;
		Terminal* clockTerminal = NULL;
		Terminal* countEnableTerminal = NULL;

		for (auto& terminal : terminals)
		{
			if (terminal.type == "counterWriteEnable")
				writeEnableTerminal = &terminal;
			else if (terminal.type == "counterClock")
				clockTerminal = &terminal;
			else if (terminal.type == "counterCountEnable")
				countEnableTerminal = &terminal;
			else if (terminal.type == "counterOut1")
				outputBit1 = &terminal;
			else if (terminal.type == "counterOut2")
				outputBit2 = &terminal;
			else if (terminal.type == "counterOut3")
				outputBit3 = &terminal;
			else if (terminal.type == "counterOut4")
				outputBit4 = &terminal;
			else if (terminal.type == "counterIn1")
				inputBit1 = &terminal;
			else if (terminal.type == "counterIn2")
				inputBit2 = &terminal;
			else if (terminal.type == "counterIn3")
				inputBit3 = &terminal;
			else if (terminal.type == "counterIn4")
				inputBit4 = &terminal;
		}

		if (writeEnableTerminal && writeEnableTerminal->state)
		{
			if (inputBit1 && inputBit2 && inputBit3 && inputBit4)
			{
				counterValue = 0;
				if (inputBit1->state)
					counterValue += 1;
				
				if (inputBit2->state)
					counterValue += 2;

				if (inputBit3->state)
					counterValue += 4;

				if (inputBit4->state)
					counterValue += 8;
			}
		}
		else
		{
			if (!counterCounted && risingEdge && countEnableTerminal && countEnableTerminal->state)
			{
				if (counterValue < 15)
					counterValue++;
				else
					counterValue = 0;

				counterCounted = true;
			}
		}

		std::string outputBinary = decimalToBinaryString(counterValue, 4);

		if (outputBit1)
		{
			if (outputBinary[3] == '1')
			{
				outputBit1->state = true;
			}
			else
			{
				outputBit1->state = false;
			}
		}
		
		if (outputBit2)
		{
			if (outputBinary[2] == '1')
				outputBit2->state = true;
			else
				outputBit2->state = false;
		}

		if (outputBit3)
		{
			if (outputBinary[1] == '1')
				outputBit3->state = true;
			else
				outputBit3->state = false;
		}
		
		if (outputBit4)
		{
			if (outputBinary[0] == '1')
				outputBit4->state = true;
			else
				outputBit4->state = false;
		}
	}

	void simulateMicrocounter()
	{
		Terminal* inputBit3 = NULL;
		Terminal* inputBit2 = NULL;
		Terminal* inputBit1 = NULL;

		Terminal* outputBit3 = NULL;
		Terminal* outputBit2 = NULL;
		Terminal* outputBit1 = NULL;

		Terminal* reset = NULL;

		for (auto& terminal : terminals)
		{
			if (terminal.type == "microcounterOut1")
				outputBit1 = &terminal;
			else if (terminal.type == "microcounterOut2")
				outputBit2 = &terminal;
			else if (terminal.type == "microcounterOut3")
				outputBit3 = &terminal;
			else if (terminal.type == "microcounterIn1")
				inputBit1 = &terminal;
			else if (terminal.type == "microcounterIn2")
				inputBit2 = &terminal;
			else if (terminal.type == "microcounterIn3")
				inputBit3 = &terminal;
			else if (terminal.type == "microcounterReset")
				reset = &terminal;
		}

		if (reset && reset->state)
			microcounterValue = 0;

		if (!microcounterCounted && fallingEdge)
		{
			if (microcounterValue < 7)
				microcounterValue++;
			else
				microcounterValue = 0;
			
			microcounterCounted = true;
		}

		std::string outputBinary = decimalToBinaryString(microcounterValue, 3);

		if (outputBit1)
			outputBit1->state = outputBinary[2] == '1';

		if (outputBit2)
			outputBit2->state = outputBinary[1] == '1';

		if (outputBit3)
			outputBit3->state = outputBinary[0] == '1';
	}

	void simulateIR()
	{
		Terminal* inputBit8 = NULL;
		Terminal* inputBit7 = NULL;
		Terminal* inputBit6 = NULL;
		Terminal* inputBit5 = NULL;
		Terminal* inputBit4 = NULL;
		Terminal* inputBit3 = NULL;
		Terminal* inputBit2 = NULL;
		Terminal* inputBit1 = NULL;

		Terminal* decodeOutputBit8 = NULL;
		Terminal* decodeOutputBit7 = NULL;
		Terminal* decodeOutputBit6 = NULL;
		Terminal* decodeOutputBit5 = NULL;

		Terminal* outputBit4 = NULL;
		Terminal* outputBit3 = NULL;
		Terminal* outputBit2 = NULL;
		Terminal* outputBit1 = NULL;

		Terminal* writeEnableBit = NULL;

		for (auto& terminal : terminals)
		{
			if (terminal.type == "IRIn1")
				inputBit1 = &terminal;
			else if (terminal.type == "IRIn2")
				inputBit2 = &terminal;
			else if (terminal.type == "IRIn3")
				inputBit3 = &terminal;
			else if (terminal.type == "IRIn4")
				inputBit4 = &terminal;
			else if (terminal.type == "IRIn5")
				inputBit5 = &terminal;
			else if (terminal.type == "IRIn6")
				inputBit6 = &terminal;
			else if (terminal.type == "IRIn7")
				inputBit7 = &terminal;
			else if (terminal.type == "IRIn8")
				inputBit8 = &terminal;
			else if (terminal.type == "IROut1")
				outputBit1 = &terminal;
			else if (terminal.type == "IROut2")
				outputBit2 = &terminal;
			else if (terminal.type == "IROut3")
				outputBit3 = &terminal;
			else if (terminal.type == "IROut4")
				outputBit4 = &terminal;
			else if (terminal.type == "IRDecodeOut5")
				decodeOutputBit5 = &terminal;
			else if (terminal.type == "IRDecodeOut6")
				decodeOutputBit6 = &terminal;
			else if (terminal.type == "IRDecodeOut7")
				decodeOutputBit7 = &terminal;
			else if (terminal.type == "IRDecodeOut8")
				decodeOutputBit8 = &terminal;
			else if (terminal.type == "IRWriteEnable")
				writeEnableBit = &terminal;
		}

		if (writeEnableBit && writeEnableBit->state && inputBit1 && inputBit2 && inputBit3 && inputBit4 && inputBit5 && inputBit6 && inputBit7 && inputBit8)
		{
			IRContents[0] = inputBit1->state;
			IRContents[1] = inputBit2->state;
			IRContents[2] = inputBit3->state;
			IRContents[3] = inputBit4->state;
			IRContents[4] = inputBit5->state;
			IRContents[5] = inputBit6->state;
			IRContents[6] = inputBit7->state;
			IRContents[7] = inputBit8->state;
		}

		if (outputBit1 && outputBit2 && outputBit3 && outputBit4 && decodeOutputBit5 && decodeOutputBit6 && decodeOutputBit7 && decodeOutputBit8)
		{
			outputBit1->state = IRContents[0];
			outputBit2->state = IRContents[1];
			outputBit3->state = IRContents[2];
			outputBit4->state = IRContents[3];
			decodeOutputBit5->state = IRContents[4];
			decodeOutputBit6->state = IRContents[5];
			decodeOutputBit7->state = IRContents[6];
			decodeOutputBit8->state = IRContents[7];
		}
	}

	void setDecoderContents(std::string binaryString)
	{
		decoderContents[0] = int(binaryString[0] - '0');
		decoderContents[1] = int(binaryString[1] - '0');
		decoderContents[2] = int(binaryString[2] - '0');
		decoderContents[3] = int(binaryString[3] - '0');
		decoderContents[4] = int(binaryString[4] - '0');
		decoderContents[5] = int(binaryString[5] - '0');
		decoderContents[6] = int(binaryString[6] - '0');
		decoderContents[7] = int(binaryString[7] - '0');
		decoderContents[8] = int(binaryString[8] - '0');
		decoderContents[9] = int(binaryString[9] - '0');
		decoderContents[10] = int(binaryString[10] - '0');
		decoderContents[11] = int(binaryString[11] - '0');
		decoderContents[12] = int(binaryString[12] - '0');
		decoderContents[13] = int(binaryString[13] - '0');
		decoderContents[14] = int(binaryString[14] - '0');
		decoderContents[15] = int(binaryString[15] - '0');
		decoderContents[16] = int(binaryString[16] - '0');
	}

	std::string decodeMicroinstruction(std::string instructionString, std::string stepString, std::string flagsString)
	{
		bool debugMode = false;

		// Universal - Memory address to program counter.
		if (stepString == "000")      
			return "01000000000001000"; // MI, CO

		// Universal - Transfer instruction from RAM to instruction register. Also increment program counter.
		if (stepString == "001")      
			return "00010100000010000"; // RO, II, CE

		// NOP (No Operation)
		if (instructionString == "0000")
		{
			if (debugMode)
				std::cout << "NOP" << std::endl;

			return "00000000000000001"; // MR
		}

		// LDA (Load A) - Load contents of a memory address into Register A.
		if (instructionString == "0001")
		{
			if (debugMode)
				std::cout << "LDA" << std::endl;

			if (stepString == "010")
				return "01001000000000000"; // MI, IO
			if (stepString == "011")
				return "00010010000000000"; // RO, AI
			if (stepString == "100")
				return "00000000000000001"; // MR
		}

		// ADD (Add) - Load contents of a memory address into Register B, add it to Register A.
		if (instructionString == "0010")
		{
			if (debugMode)
				std::cout << "ADD" << std::endl;

			if (stepString == "010")
				return "01001000000000000"; // MI, IO
			if (stepString == "011")
				return "00010000001000000"; // RO, BI
			if (stepString == "100")
				return "00000010100000010"; // AI, EO, FI
		}

		// SUB (Subtract) - Load contents of a memory address into Register B, subtract it from Register A.
		if (instructionString == "0011")
		{
			if (debugMode)
				std::cout << "SUB" << std::endl;

			if (stepString == "010")
				return "01001000000000000"; // MI, IO
			if (stepString == "011")
				return "00010000001000000"; // RO, BI
			if (stepString == "100")
				return "00000010110000010"; // AI, EO, SU, FI
		}

		// STA (Store A) - Store the contents of Register A at a RAM address.
		if (instructionString == "0100")
		{
			if (debugMode)
				std::cout << "STA" << std::endl;

			if (stepString == "010")
				return "01001000000000000"; // MI, IO
			if (stepString == "011")
				return "00100001000000000"; // AO, RI
			if (stepString == "100")
				return "00000000000000001"; // MR
		}

		// LDI (Load Immediate) - Load a provided value into Register A.
		if (instructionString == "0101")
		{
			if (debugMode)
				std::cout << "LDI" << std::endl;

			if (stepString == "010")
				return "00001010000000000"; // IO, AI
			if (stepString == "011")
				return "00000000000000001"; // MR
		}

		// JMP (Jump) - Load a provided value into the program counter.
		if (instructionString == "0110")
		{
			if (debugMode)
				std::cout << "JMP" << std::endl;

			if (stepString == "010")
				return "00001000000000100"; // IO, JU 
			if (stepString == "011")
				return "00000000000000001"; // MR
		}

		// JC (Jump on Carry) - Jump only if the carry flag is set.
		if (instructionString == "0111")
		{
			if (debugMode)
				std::cout << "JC" << std::endl;

			if (stepString == "010")
				if (flagsString[0] == '1')
					return "00001000000000100"; // IO, JU 
				else if (flagsString[0] == '0')
					return "00000000000000001"; // MR
			if (stepString == "011")
				return "00000000000000001"; // MR
		}

		// JC (Jump on Zero) - Jump only if the zero flag is set.
		if (instructionString == "1000")
		{
			if (debugMode)
				std::cout << "JC" << std::endl;

			if (stepString == "010")
				if (flagsString[1] == '1')
					return "00001000000000100"; // IO, JU 
				else if (flagsString[1] == '0')
					return "00000000000000001"; // MR
			if (stepString == "011")
				return "00000000000000001"; // MR
		}

		// OUT (Output) - Transfer the contents of Register A to the display.
		if (instructionString == "1110")
		{
			if (debugMode)
				std::cout << "OUT" << std::endl;

			if (stepString == "010")
				return "00000001000100000"; // AO, OI
			if (stepString == "011")
				return "00000000000000001"; // MR
		}

		// HLT (Halt) - Stop the clock.
		if (instructionString == "1111")
		{
			if (debugMode)
				std::cout << "HLT" << std::endl;

			if (stepString == "010")
				return "10000000000000000"; // HT
			if (stepString == "011")
				return "00000000000000001"; // MR
		}

		return "00000000000000001"; // MR
	}

	void simulateDecoder()
	{
		Terminal* inputBit9 = NULL;
		Terminal* inputBit8 = NULL;
		Terminal* inputBit7 = NULL;
		Terminal* inputBit6 = NULL;
		Terminal* inputBit5 = NULL;
		Terminal* inputBit4 = NULL;
		Terminal* inputBit3 = NULL;
		Terminal* inputBit2 = NULL;
		Terminal* inputBit1 = NULL;

		Terminal* outputBit17 = NULL;
		Terminal* outputBit16 = NULL;
		Terminal* outputBit15 = NULL;
		Terminal* outputBit14 = NULL;
		Terminal* outputBit13 = NULL;
		Terminal* outputBit12 = NULL;
		Terminal* outputBit11 = NULL;
		Terminal* outputBit10 = NULL;
		Terminal* outputBit9 = NULL;
		Terminal* outputBit8 = NULL;
		Terminal* outputBit7 = NULL;
		Terminal* outputBit6 = NULL;
		Terminal* outputBit5 = NULL;
		Terminal* outputBit4 = NULL;
		Terminal* outputBit3 = NULL;
		Terminal* outputBit2 = NULL;
		Terminal* outputBit1 = NULL;

		for (auto& terminal : terminals)
		{
			if (terminal.type == "decoderIn1")
				inputBit1 = &terminal;
			else if (terminal.type == "decoderIn2")
				inputBit2 = &terminal;
			else if (terminal.type == "decoderIn3")
				inputBit3 = &terminal;
			else if (terminal.type == "decoderIn4")
				inputBit4 = &terminal;
			else if (terminal.type == "decoderIn5")
				inputBit5 = &terminal;
			else if (terminal.type == "decoderIn6")
				inputBit6 = &terminal;
			else if (terminal.type == "decoderIn7")
				inputBit7 = &terminal;
			else if (terminal.type == "decoderIn8")
				inputBit8 = &terminal;
			else if (terminal.type == "decoderIn9")
				inputBit9 = &terminal;
			else if (terminal.type == "decoderOut1")
				outputBit1 = &terminal;
			else if (terminal.type == "decoderOut2")
				outputBit2 = &terminal;
			else if (terminal.type == "decoderOut3")
				outputBit3 = &terminal;
			else if (terminal.type == "decoderOut4")
				outputBit4 = &terminal;
			else if (terminal.type == "decoderOut5")
				outputBit5 = &terminal;
			else if (terminal.type == "decoderOut6")
				outputBit6 = &terminal;
			else if (terminal.type == "decoderOut7")
				outputBit7 = &terminal;
			else if (terminal.type == "decoderOut8")
				outputBit8 = &terminal;
			else if (terminal.type == "decoderOut9")
				outputBit9 = &terminal;
			else if (terminal.type == "decoderOut10")
				outputBit10 = &terminal;
			else if (terminal.type == "decoderOut11")
				outputBit11 = &terminal;
			else if (terminal.type == "decoderOut12")
				outputBit12 = &terminal;
			else if (terminal.type == "decoderOut13")
				outputBit13 = &terminal;
			else if (terminal.type == "decoderOut14")
				outputBit14 = &terminal;
			else if (terminal.type == "decoderOut15")
				outputBit15 = &terminal;
			else if (terminal.type == "decoderOut16")
				outputBit16 = &terminal;
			else if (terminal.type == "decoderOut17")
				outputBit17 = &terminal;
		}

		if (inputBit1 && inputBit2 && inputBit3 && inputBit4 && inputBit5 && inputBit6 && inputBit7 && inputBit8 && inputBit9)
		{
			// Decode here
			std::string instructionString = "";
			std::string stepString = "";
			std::string flagsString = "";
			std::vector<int> inputs = { inputBit1->state, inputBit2->state, inputBit3->state, inputBit4->state, inputBit5->state, inputBit6->state, inputBit7->state, inputBit8->state, inputBit9->state };
			
			for (int i = 0; i < inputs.size(); i++)
			{
				if (i < 3)
					stepString += std::to_string(inputs[i]);
				else if (i < 7)
					instructionString += std::to_string(inputs[i]);
				else
					flagsString += std::to_string(inputs[i]);
			}

			std::string outputString = decodeMicroinstruction(instructionString, stepString, flagsString);
			setDecoderContents(outputString);
		}

		if (outputBit1 && outputBit2 && outputBit3 && outputBit4 && outputBit5 && outputBit6 && outputBit7 && outputBit8 && outputBit9 && outputBit10 && outputBit11 && outputBit12 && outputBit13 && outputBit14 && outputBit15 && outputBit16 && outputBit17)
		{
			outputBit1->state = decoderContents[0];
			outputBit2->state = decoderContents[1];
			outputBit3->state = decoderContents[2];
			outputBit4->state = decoderContents[3];
			outputBit5->state = decoderContents[4];
			outputBit6->state = decoderContents[5];
			outputBit7->state = decoderContents[6];
			outputBit8->state = decoderContents[7];
			outputBit9->state = decoderContents[8];
			outputBit10->state = decoderContents[9];
			outputBit11->state = decoderContents[10];
			outputBit12->state = decoderContents[11];
			outputBit13->state = decoderContents[12];
			outputBit14->state = decoderContents[13];
			outputBit15->state = decoderContents[14];
			outputBit16->state = decoderContents[15];
			outputBit17->state = decoderContents[16];
		}
	}

	void simulateFlagsReg()
	{
		Terminal* inputBit1 = NULL;
		Terminal* inputBit2 = NULL;

		Terminal* outputBit1 = NULL;
		Terminal* outputBit2 = NULL;

		Terminal* writeEnable = NULL;

		for (auto& terminal : terminals)
		{
			if (terminal.type == "flagsRegIn1")
				inputBit1 = &terminal;
			else if (terminal.type == "flagsRegIn2")
				inputBit2 = &terminal;
			else if (terminal.type == "flagsRegOut1")
				outputBit1 = &terminal;
			else if (terminal.type == "flagsRegOut2")
				outputBit2 = &terminal;
			else if (terminal.type == "flagsRegWriteEnable")
				writeEnable = &terminal;
		}

		if (inputBit1 && inputBit2 && outputBit1 && outputBit2 && writeEnable)
		{
			if (writeEnable->state)
			{
				flagsRegContents[0] = inputBit1->state;
				flagsRegContents[1] = inputBit2->state;
			}

			outputBit1->state = flagsRegContents[0];
			outputBit2->state = flagsRegContents[1];
		}
	}

	void simulateDisplay()
	{
		Terminal* inputBit8 = NULL;
		Terminal* inputBit7 = NULL;
		Terminal* inputBit6 = NULL;
		Terminal* inputBit5 = NULL;
		Terminal* inputBit4 = NULL;
		Terminal* inputBit3 = NULL;
		Terminal* inputBit2 = NULL;
		Terminal* inputBit1 = NULL;

		Terminal* writeEnableBit = NULL;

		for (auto& terminal : terminals)
		{
			if (terminal.type == "displayIn1")
				inputBit1 = &terminal;
			else if (terminal.type == "displayIn2")
				inputBit2 = &terminal;
			else if (terminal.type == "displayIn3")
				inputBit3 = &terminal;
			else if (terminal.type == "displayIn4")
				inputBit4 = &terminal;
			else if (terminal.type == "displayIn5")
				inputBit5 = &terminal;
			else if (terminal.type == "displayIn6")
				inputBit6 = &terminal;
			else if (terminal.type == "displayIn7")
				inputBit7 = &terminal;
			else if (terminal.type == "displayIn8")
				inputBit8 = &terminal;
			else if (terminal.type == "displayWriteEnable")
				writeEnableBit = &terminal;
		}

		if (writeEnableBit && writeEnableBit->state && inputBit1 && inputBit2 && inputBit3 && inputBit4 && inputBit5 && inputBit6 && inputBit7 && inputBit8)
		{
			displayContents[7] = inputBit1->state;
			displayContents[6] = inputBit2->state;
			displayContents[5] = inputBit3->state;
			displayContents[4] = inputBit4->state;
			displayContents[3] = inputBit5->state;
			displayContents[2] = inputBit6->state;
			displayContents[1] = inputBit7->state;
			displayContents[0] = inputBit8->state;
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
				Terminal* thisTerminalA = findTerminal(connection.terminalA);
				if (thisTerminalA)
				{
					if (thisTerminalA->type == "gatedOut")
						sourceConnections.push_back(&connection);

					if (("aluOut1" <= thisTerminalA->type && thisTerminalA->type <= "aluOut8"))
					{
						sourceConnections.push_back(&connection);
					}

					if (("ramOut1" <= thisTerminalA->type && thisTerminalA->type <= "ramOut8"))
					{
						sourceConnections.push_back(&connection);
					}

					if (("counterOut1" <= thisTerminalA->type && thisTerminalA->type <= "counterOut4"))
					{
						sourceConnections.push_back(&connection);
					}

					if (("microcounterOut1" <= thisTerminalA->type && thisTerminalA->type <= "microcounterOut3"))
					{
						sourceConnections.push_back(&connection);
					}

					if (("IROut1" <= thisTerminalA->type && thisTerminalA->type <= "IROut4"))
					{
						sourceConnections.push_back(&connection);
					}

					if (thisTerminalA->type.find("decoderOut") != std::string::npos)
					{
						sourceConnections.push_back(&connection);
					}

					if (("flagsRegOut1" <= thisTerminalA->type && thisTerminalA->type <= "flagsRegOut2"))
					{
						sourceConnections.push_back(&connection);
					}

					if (thisTerminalA->type == "aluZeroFlagOut" || thisTerminalA->type == "aluCarryFlagOut")
					{
						sourceConnections.push_back(&connection);
					}

					if (("IRDecodeOut5" <= thisTerminalA->type && thisTerminalA->type <= "IRDecodeOut8"))
					{
						sourceConnections.push_back(&connection);
					}
				}
			}
		}
	}

	olc::vf2d GetWorldMouse()
	{
		olc::vf2d worldMouse;
		pz.ScreenToWorld(GetMousePos(), worldMouse);
		return worldMouse;
	}

	void deleteClosest()
	{
		double smallestDistance = 0.00;
		int closestConnectionId = 0;
		int closestConnectionNotOutTerminalId = 0;
		bool noConnectionDeleted = true;

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

			noConnectionDeleted = false;
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
				bool terminalsRemaining = true;

				while (terminalsRemaining)
				{
					bool terminalsFound = false;
					for (auto terminal : terminals)
					{
						if (terminal.componentId == closestId)
							terminalsFound = true;
					}

					if (!terminalsFound)
						terminalsRemaining = false;

					for (auto iter = terminals.begin(); iter != terminals.end(); ++iter)
					{
						if (iter->componentId == closestId)
						{
							bool connectionsRemaining = true;

							while (connectionsRemaining)
							{
								bool connectionsFound = false;
								for (auto connection : connections)
								{
									if (connection.terminalA == iter->id || connection.terminalB == iter->id)
										connectionsFound = true;
								}

								if (!connectionsFound)
									connectionsRemaining = false;

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
		updateSimulation = true;
	}

	void programRAM()
	{
		double smallestDistance = 0.00;
		int closestRamAddress = 0;
		int closestRamBit = 0;

		float bitPadding = 25;
		float ramToBitsPadding = 20;

		olc::vi2d ramWorldPos = { 0, 0 };

		for (auto component : components) {
			if (component.type == "RAM")
				ramWorldPos = component.pos;
		}

		for (int ramAddress = 0; ramAddress < ramContents.size(); ramAddress++)
		{
			for (int ramBit = 0; ramBit < ramContents[0].size(); ramBit++)
			{
				olc::vf2d thisBitPosition = { ramToBitsPadding + ramBit * bitPadding, ramToBitsPadding + ramAddress * bitPadding };
				double distance = CalculateDistance(ramWorldPos + thisBitPosition, GetWorldMouse());

				if (distance < smallestDistance || smallestDistance == 0.00)
				{

					smallestDistance = distance;
					closestRamAddress = ramAddress;
					closestRamBit = ramBit;
				}
			}
		}

		if (smallestDistance < 10.00)
		{
			ramContents[closestRamAddress][closestRamBit] = !ramContents[closestRamAddress][closestRamBit];
		}
	}

	void simulateClock()
	{
		Terminal* clockHaltTerminal = NULL;

		for (auto& terminal : terminals)
		{
			if (terminal.type == "clockHalt")
			{
				clockHaltTerminal = &terminal;
				break;
			}
		}

		if (clockHaltTerminal && clockHaltTerminal->state)
		{
			clockState = false;
			clockSpeed = 0;
			clockTicks = 0;
		}
			
		bool oldState = clockState;

		if (clockSpeed)
		{
			clockTicks++;

			if (clockTicks >= clockSpeed * 100 || clockSpeed == 1)
			{
				if (clockState)
					clockState = false;
				else
					clockState = true;

				clockTicks = 0;
				updateSimulation = true;
				redrawRequired = true;
			}
		}
		else
		{
			if (GetKey(olc::Key::O).bReleased)
			{
				if (clockState)
					clockState = false;
				else
					clockState = true;

				updateSimulation = true;
				redrawRequired = true;
			}
		}
		
		bool newState = clockState;
		bool oldRisingEdge = risingEdge;
		bool oldFallingEdge = fallingEdge;

		if (!oldState && newState)
			risingEdge = true;
		else
			risingEdge = false;

		if (oldState && !newState)
			fallingEdge = true;
		else
			fallingEdge = false;

		if (oldRisingEdge != risingEdge || oldFallingEdge != fallingEdge)
			redrawRequired = true;
	}

	std::vector<std::vector<int>> loadProgram(std::string programName)
	{
		std::vector<std::vector<int>> blank = {
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 } };

		std::vector<std::vector<int>> add = {
		{ 0, 0, 0, 1, 1, 1, 1, 0 },
		{ 0, 0, 1, 0, 1, 1, 1, 1 },
		{ 1, 1, 1, 0, 0, 0, 0, 0 },
		{ 1, 1, 1, 1, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 1, 0 },
		{ 0, 0, 0, 0, 0, 1, 0, 0 } };

		std::vector<std::vector<int>> multiplesOfThree = {
		{ 0, 1, 0, 1, 0, 0, 1, 1 },
		{ 0, 1, 0, 0, 1, 1, 1, 1 },
		{ 0, 1, 0, 1, 0, 0, 0, 0 },
		{ 0, 0, 1, 0, 1, 1, 1, 1 },
		{ 1, 1, 1, 0, 0, 0, 0, 0 },
		{ 0, 1, 1, 0, 0, 0, 1, 1 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 } };

		std::vector<std::vector<int>> multiplesOfSix = {
		{ 0, 1, 0, 1, 0, 1, 1, 0 },
		{ 0, 1, 0, 0, 1, 1, 1, 1 },
		{ 0, 1, 0, 1, 0, 0, 0, 0 },
		{ 0, 0, 1, 0, 1, 1, 1, 1 },
		{ 1, 1, 1, 0, 0, 0, 0, 0 },
		{ 0, 1, 1, 0, 0, 0, 1, 1 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 } };

		std::vector<std::vector<int>> conditionals = {
		{ 1, 1, 1, 0, 0, 0, 0, 0 },
		{ 0, 0, 1, 0, 1, 1, 1, 1 },
		{ 0, 1, 1, 1, 0, 1, 0, 0 },
		{ 0, 1, 1, 0, 0, 0, 0, 0 },
		{ 0, 0, 1, 1, 1, 1, 1, 1 },
		{ 1, 1, 1, 0, 0, 0, 0, 0 },
		{ 1, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 1, 1, 0, 0, 1, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 1, 0, 1, 0, 0 } };

		std::vector<std::vector<int>> multiply = {
		{ 0, 0, 0, 1, 1, 1, 1, 0 },
		{ 0, 0, 1, 1, 1, 1, 0, 0 },
		{ 0, 1, 1, 1, 0, 1, 1, 0 },
		{ 0, 0, 0, 1, 1, 1, 0, 1 },
		{ 1, 1, 1, 0, 0, 0, 0, 0 },
		{ 1, 1, 1, 1, 0, 0, 0, 0 },
		{ 0, 1, 0, 0, 1, 1, 1, 0 },
		{ 0, 0, 0, 1, 1, 1, 0, 1 },
		{ 0, 0, 1, 0, 1, 1, 1, 1 },
		{ 0, 1, 0, 0, 1, 1, 0, 1 },
		{ 0, 1, 1, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 1 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 1, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 1, 0 } };

		std::vector<std::vector<int>> divide = {
		{ 0, 0, 0, 1, 1, 1, 1, 0 },
		{ 0, 0, 1, 1, 1, 1, 1, 1 },
		{ 1, 0, 0, 0, 0, 1, 0, 0 },
		{ 0, 1, 1, 0, 0, 1, 1, 0 },
		{ 0, 0, 0, 1, 1, 1, 0, 0 },
		{ 1, 1, 1, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 1, 1, 1, 1, 0 },
		{ 0, 0, 1, 0, 1, 1, 0, 1 },
		{ 0, 1, 0, 0, 1, 1, 1, 0 },
		{ 0, 1, 0, 1, 0, 0, 0, 1 },
		{ 0, 0, 1, 0, 1, 1, 0, 0 },
		{ 0, 1, 0, 0, 1, 1, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 1 },
		{ 0, 0, 0, 0, 0, 1, 0, 0 },
		{ 0, 0, 0, 0, 0, 1, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 1, 0 } };

		std::vector<std::vector<int>> fibonacci = {
		{ 0, 1, 0, 1, 0, 0, 0, 1 },
		{ 0, 1, 0, 0, 1, 1, 1, 0 },
		{ 0, 1, 0, 1, 0, 0, 0, 0 },
		{ 0, 1, 0, 0, 1, 1, 1, 1 },
		{ 1, 1, 1, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 1, 1, 1, 1, 0 },
		{ 0, 0, 1, 0, 1, 1, 1, 1 },
		{ 0, 1, 0, 0, 1, 1, 1, 0 },
		{ 1, 1, 1, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 1, 1, 1, 1, 1 },
		{ 0, 0, 1, 0, 1, 1, 1, 0 },
		{ 0, 1, 1, 1, 1, 1, 0, 1 },
		{ 0, 1, 1, 0, 0, 0, 1, 1 },
		{ 1, 1, 1, 1, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 } };

		if (programName == "add")
			return add;

		if (programName == "multiplesOfThree")
			return multiplesOfThree;

		if (programName == "multiplesOfSix")
			return multiplesOfSix;

		if (programName == "conditionals")
			return conditionals;

		if (programName == "multiply")
			return multiply;

		if (programName == "divide")
			return divide;

		if (programName == "fibonacci")
			return fibonacci;
		
		return blank;
	}
};

int main()
{
	Viscom vc;
	if (vc.Construct(1600, 900, 1, 1, false))
		vc.Start();

	return 0;
}
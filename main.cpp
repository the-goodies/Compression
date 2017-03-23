#include <experimental/filesystem> // std::experimental::filesystem, kas leidžia gauti programos vardą be kelio ir plėtinio
#include <Windows.h>
#include "compression.h"
#include <cstdio>
#include <fstream>
#include <string>
#include <chrono> // skaiciuoti laika
#include <cstdlib> // exit, EXIT_FAILURE
#include <random>

using std::cout;
using std::endl;
using std::cin;
using std::string;


static void naudojimo_instrukcija(string ProgramName)
{
	cout << "Failo suspaudimas, naudojimas:\n\n";
	cout << "  " << ProgramName << " compress failo_pav suspausto_failo_pav [/fast | /full] [/encrypt]\n";
	cout << "  " << ProgramName << " decompress suspausto_failo_pav išskleisto_failo_pav\n";
	cout << "  \n";
	cout << "  Daugiau info: " << ProgramName << " /?\n\n";
}

static void issami_instrukcija(string ProgramName)
{
	cout << "Naudojimas:\n\n";
	cout << "  " << ProgramName << " compress failo_pav suspausto_failo_pav [/fast | /full] [/encrypt]\n";
	cout << "  " << ProgramName << " decompress suspausto_failo_pav išskleisto_failo_pav\n";
	cout << "  \n";
	cout << "  Failo suspaudimo programa su galimybe spaudžiamą failą užšifruoti: /encrypt\n";
	cout << "  Papildomai galima nurodyti suspaudimo lygį - /full arba /fast:\n";
	cout << "  /full (numatytasis) suspaudimo lygis suspaudžia failą geriau nei /fast, bet\n";
	cout << "  spaudimas/išskleidimas vyksta atitinkamai lėčiau\n";
	cout << "  Išskleidžiant failą nereikia nurodyti failo suspaudimo lygį\n";
	cout << "  \n";
	cout << "  Vietoj compress/decompress galima atitinkamai naudoti -/+, pvz:\n";
	cout << "  " << ProgramName << " - pavyzdys.txt pavyzdys.cmp\n";
	cout << "  " << ProgramName << " + pavyzdys.cmp pavyzdys.out\n";
	cout << "  pavyzdys.txt failas bus suspaustas /full lygiu į pavyzdys.cmp failą\n";
	cout << "  ir vėl išskleistas į pavyzdys.out failą\n";
	cout << "  pavyzdys.txt failas bus identiškas pavyzdys.out failui\n";
	cout << "  \n";
}

struct fileContents
{
	s64 size;
	u8* memory;
};

static fileContents readEntireFileToMemory(const char* fileName)
{
	std::ifstream file(fileName, std::fstream::binary | std::fstream::in);

	if (!file.is_open())
	{
		cout << "Duotas failas " << fileName << " nerastas arba nėra privilegijų jo atidaryti.";
		exit(EXIT_FAILURE);
	}

	file.seekg(0, file.end);
	std::streampos file_size = file.tellg();
	file.seekg(0, file.beg);

	fileContents result;
	result.memory = (u8*) malloc((std::size_t)file_size);
	file.read((char*)result.memory, file_size);
	result.size = file_size;

	file.close();
	return result;
}

// kad nesimatytu kai vedamas password
void disableConsoleOutput(bool disable = true)
{
	HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
	DWORD mode;
	GetConsoleMode(hStdin, &mode);

	if (disable) mode &= ~ENABLE_ECHO_INPUT;
	else		 mode |= ENABLE_ECHO_INPUT;

	SetConsoleMode(hStdin, mode);
}

const u8 BOM = 0b01010100;


static void xor_buffer(u8* inBuffer, s64 size, std::mt19937_64 & cipher)
{
	u64* buffer = (u64*)inBuffer;
	for (s64 i = 0; i < size / 8; ++i)
	{
		*buffer++ ^= cipher();
	}

		
	u64 last_cipher = cipher();
	inBuffer = (u8*)buffer;
	size = size % 8;
	for (s64 i = 0; i < size; ++i)
	{
		*inBuffer++ ^= *(((u8*)(&last_cipher)) + i);
	}
}

static std::mt19937_64 requestPassword()
{
	string password;
	cout << "Įveskite slaptažodį: ";
	disableConsoleOutput(true);
	std::getline(cin, password);
	disableConsoleOutput(false);
	cout << "\n";
	std::seed_seq seed(password.begin(), password.end());
	std::mt19937_64 cipher(seed);
	return cipher;
}


// first time called sets the timer and returns 0
// second time returns elapsed time (second call time - first call time)
// third time and so on repeats this cycle
double getElapsedTime()
{
	using get_time = std::chrono::steady_clock;
	static get_time::time_point start_time;
	static bool start = true;
	if (start)
	{
		start_time = get_time::now();
		start = !start;
		return 0.0;
	}
	else
	{
		end_time = get_time::now();
		start = !start;
		return double(std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count());
	}
}


int main(int argCount, char** args)
{
	setlocale(LC_ALL, "Lithuanian");

	namespace fs = std::experimental::filesystem;
	fs::path Program = fs::path(args[0]);
	Program.replace_extension(); // pasalina .exe extension
	string ProgramName = Program.filename().string();

 	if (argCount == 2)
	{
		if (strcmp(args[1], "/?") == 0)
		{
			issami_instrukcija(ProgramName);
			exit(EXIT_FAILURE);
		}
		else naudojimo_instrukcija(ProgramName);
	}
	else if (argCount == 4)
	{
		char* command = args[1];
		char* inFileName = args[2];
		char* outFileName = args[3];


		filenames files = { inFileName, outFileName };
		fileContents inFile = readEntireFileToMemory(inFileName);
		fileContents outFile;
		
		if (strcmp(command, "compress") == 0 || strcmp(command, "-") == 0)
		{
			outFile.size = inFile.size * 2;
			outFile.memory = (u8*)malloc((std::size_t)outFile.size);

			s64 byte_pos = 0;
			writeByte(BOM, outFile.memory, byte_pos, 0);
			writeFourBytes((u32)inFile.size, outFile.memory, byte_pos, 0);

			auto start_time = getElapsedTime();
			s64 compressed_size = compress(inFile.memory, inFile.size, outFile.memory + 5, outFile.size - 5, files);
			s64 outFile_final_size = compressed_size + 5;
			std::ofstream file(outFileName, std::fstream::binary | std::fstream::out);
			file.write((char*)outFile.memory, outFile_final_size);
			file.close();

			auto end_time = getElapsedTime();
			// spausdinti kiek laiko praejo
			cout << "Užtruko " << std::setprecision(2) << end_time / 1000.0f << " sekundes" << endl;
		}
		else if (strcmp(command, "decompress") == 0 || strcmp(command, "+") == 0)
		{
			s64 byte_pos = 0;
			u8 first_byte = readByte(inFile.memory, byte_pos, 0);
			if (first_byte != BOM && first_byte != BOM + 1)
			{
				cout << "Duotas failas " << inFileName << " nebuvo suspaustas su šia programa\nNeįmanoma jo išskleisti";
				exit(EXIT_FAILURE);
			}

			bool encrypted = false;
			if (first_byte == BOM + 1)
			{

				std::mt19937_64 cipher = requestPassword();

				xor_buffer(inFile.memory + 1, inFile.size - 1, cipher);

				u8 second_byte = readByte(inFile.memory, byte_pos, 0);
				if (second_byte != BOM)
				{
					cout << "Neteisingas slaptažodis";
					exit(EXIT_FAILURE);
				}
				encrypted = true;
			}

			outFile.size = readFourBytes(inFile.memory, byte_pos, 0);
			outFile.memory = (u8*)malloc((std::size_t)outFile.size);

			auto start_time = getElapsedTime();
			s64 decompressed_size = decompress(inFile.memory +(encrypted ? 6: 5), inFile.size - (encrypted ? 6 : 5), outFile.memory, outFile.size, files);
			std::ofstream file(outFileName, std::fstream::binary | std::fstream::out);
			file.write((char*)outFile.memory, decompressed_size);
			file.close();

			auto end_time = getElapsedTime();
			// spausdinti kiek laiko praejo
			cout << "Užtruko " << std::setprecision(2) << end_time / 1000.0f << " sekundes" << endl;
		}
		else
		{
			naudojimo_instrukcija(ProgramName);
			exit(EXIT_FAILURE);
		}
	}
	else if (argCount == 5)
	{
		char* command = args[1];
		char* encrypt = args[4];
		char* inFileName = args[2];
		char* outFileName = args[3];

		if (strcmp(encrypt, "/encrypt") != 0)
		{
			naudojimo_instrukcija(ProgramName);
			exit(EXIT_FAILURE);
		}
	

		filenames files = { inFileName, outFileName };
		fileContents inFile = readEntireFileToMemory(inFileName);
		fileContents outFile;

		if (strcmp(command, "compress") == 0 || strcmp(command, "-") == 0)
		{
			outFile.size = inFile.size * 2;
			outFile.memory = (u8*)malloc((std::size_t)outFile.size);

			s64 byte_pos = 0;
			writeByte(BOM + 1, outFile.memory, byte_pos, 0);
			writeByte(BOM, outFile.memory, byte_pos, 0);
			writeFourBytes((u32)inFile.size, outFile.memory, byte_pos, 0);

			std::mt19937_64 cipher = requestPassword();

			auto start_time = getElapsedTime();
			s64 compressed_size = compress(inFile.memory, inFile.size, outFile.memory + 6, outFile.size - 6, files);
			s64 outFile_final_size = compressed_size + 6;

			xor_buffer(outFile.memory + 1, outFile_final_size - 1, cipher);

			std::ofstream file(outFileName, std::fstream::binary | std::fstream::out);
			file.write((char*)outFile.memory, outFile_final_size);
			file.close();

			auto end_time = getElapsedTime();
			// spausdinti kiek laiko praejo			
			cout << "Užtruko " << std::setprecision(2) << end_time / 1000.0f << " sekundes" << endl;
		}
		else if (strcmp(command, "decompress") == 0 || strcmp(command, "+") == 0)
		{
			cout << "Ar norėjot suspausti " << inFileName << " failą ? " << " su /encrypt funkcija failo išskleisti negalima";
			exit(EXIT_FAILURE);
		}
		else
		{
			naudojimo_instrukcija(ProgramName);
			exit(EXIT_FAILURE);
		}
	}
	else
	{
		naudojimo_instrukcija(ProgramName);
		exit(EXIT_FAILURE);
	}
}
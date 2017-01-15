#include <experimental/filesystem> // std::experimental::filesystem, kas leidžia gauti programos vardą be kelio ir plėtinio
//#include "lib/bitstream.h"
#include "lib/Array.h"
#include "compression.h"
#include <cstdio>
#include <fstream>
#include <string>
#include <cstdlib> // exit, EXIT_FAILURE

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



int main(int argCount, char** args)
{
	setlocale(LC_ALL, "Lithuanian");

	namespace fs = std::experimental::filesystem;
	fs::path Program = fs::path(args[0]);
	Program.replace_extension(); // pasalina .exe extension
	string ProgramName = Program.filename().string();


 	if (argCount == 2)
	{
		if (strcmp(args[1], "/?") == 0) issami_instrukcija(ProgramName);
		else naudojimo_instrukcija(ProgramName);
	}
	else if (argCount == 4)
	{
		char* command = args[1];
		char* inFileName = args[2];
		char* outFileName = args[3];
		fileContents inFile = readEntireFileToMemory(inFileName);
		fileContents outFile;
		outFile.size = inFile.size * 2;
		outFile.memory = (u8*)malloc((std::size_t)outFile.size);


		if (strcmp(command, "compress") == 0 || strcmp(command, "-") == 0)
		{
			s64 compressed_size = compress(inFile.memory, inFile.size, outFile.memory, outFile.size);
			std::ofstream file(outFileName, std::fstream::binary | std::fstream::out);
			file.write((char*)outFile.memory, compressed_size);
			file.close();
		}
		else if (strcmp(command, "decompress") == 0 || strcmp(command, "+") == 0)
		{
			s64 compressed_size = decompress(inFile.memory, inFile.size, outFile.memory, outFile.size);
			std::ofstream file(outFileName, std::fstream::binary | std::fstream::out);
			file.write((char*)outFile.memory, compressed_size);
			file.close();
		}
		else
		{
			naudojimo_instrukcija(ProgramName);
			exit(EXIT_FAILURE);
		}
	}
	else if (argCount == 5)
	{

	}
	else if (argCount == 6)
	{

	}
	else naudojimo_instrukcija(ProgramName);
	
	system("pause");
}

/*[12p.] Program który otrzymuje co najmniej dwa argumenty: ścieżkę źródłową oraz ścieżkę docelową .
 Jeżeli któraś ze ścieżek nie jest katalogiem program powraca natychmiast z komunikatem błędu. 
 W przeciwnym wypadku staje się demonem. Demon wykonuje następujące czynności: śpi przez pięć minut 
 (czas spania można zmieniać przy pomocy dodatkowego opcjonalnego argumentu), 
 po czym po obudzeniu się porównuje katalog źródłowy z katalogiem docelowym. 
 Pozycje które nie są zwykłymi plikami są ignorowane (np. katalogi i dowiązania symboliczne). 
 Jeżeli demon (a) napotka na  nowy plik w katalogu źródłowym,
  i tego pliku brak w katalogu docelowym lub (b) plik w katalogu źrodłowym ma późniejszą datę ostatniej modyfikacji
   demon wykonuje kopię pliku z  katalogu źródłowego do katalogu docelowego - ustawiając w katalogu docelowym datę modyfikacji
   tak aby przy kolejnym obudzeniu nie trzeba było wykonać kopii (chyba  ze plik w katalogu źródłowym zostanie ponownie zmieniony).
    Jeżeli zaś odnajdzie plik w katalogu docelowym, którego nie ma w katalogu źródłowym to usuwa ten plik z katalogu docelowego. 
    Możliwe jest również natychmiastowe obudzenie się demona poprzez wysłanie mu sygnału SIGUSR1. 
    Wyczerpująca informacja o każdej akcji typu uśpienie/obudzenie się demona (naturalne lub w wyniku sygnału),
     wykonanie kopii lub usunięcie pliku jest przesłana do logu systemowego. Informacja ta powinna zawierać aktualną datę.

a) [10p.]  Dodatkowa opcja -R pozwalająca na rekurencyjną synchronizację katalogów (teraz pozycje będące katalogami nie są ignorowane).
 W szczególności jeżeli demon  stwierdzi w katalogu docelowym  podkatalog którego brak w katalogu źródłowym powinien usunąć go wraz 
 z zawartością.
b) [12p.] W zależności  od rozmiaru plików dla małych plików wykonywane jest kopiowanie przy pomocy read/write
 a w przypadku dużych przy pomocy mmap/write (plik źródłowy) zostaje zamapowany w całości w pamięci. 
 Próg dzielący pliki małe od dużych  może być przekazywany jako opcjonalny argument.

Uwagi: (a) Wszelkie operacje na plikach i tworzenie demona należy wykonywać
 przy pomocy API Linuksa a nie standardowej biblioteki języka C (b) 
  kopiowanie za każdym obudzeniem całego drzewa katalogów zostanie potraktowane jako poważny błąd
   (c) podobnie jak przerzucenie części zadań na shell systemowy (funkcja system).*/
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <utime.h>
#include <stdbool.h>
#include <syslog.h>
#include <sys/mman.h>
#define BUFFER_SIZE 4096 // Domyślny rozmiar bufora
void copyFileUsingRead(const char *sourcePath, const char *destinationPath);
void copyFileUsingMmap(const char *sourcePath, const char *destinationPath);
int deleteFile(const char *filePath) {
    // Usuwanie pliku
    if (unlink(filePath) == -1) {
        perror("Failed to delete file");
        return 0; // Zwraca 0 w przypadku błędu
    }

    return 1; // Zwraca 1 w przypadku powodzenia
}
struct dirent **readNormalFiles(DIR *dir, int *num_entries) {
    struct dirent **entries = NULL;
    *num_entries = 0;

    struct dirent *entry;
    while((entry = readdir(dir)) != NULL) {
        if(entry->d_type == DT_REG) {
            (*num_entries)++;
            entries = realloc(entries, sizeof(struct dirent *) * (*num_entries));
            if (entries == NULL) {
                perror("Failed to allocate memory");
                exit(EXIT_FAILURE);
            }
            entries[*num_entries - 1] = entry;
        }
    }
    rewinddir(dir); // reset directory stream to the beginning
    return entries;
}
struct dirent **readDirs(DIR *dir, int *num_entries) {
    struct dirent **entries = NULL;
    *num_entries = 0;

    struct dirent *entry;
    while((entry = readdir(dir)) != NULL) {
        if(entry->d_type == DT_DIR && strcmp(entry->d_name,".") != 0 && strcmp(entry->d_name,"..") != 0) {
            (*num_entries)++;
            entries = realloc(entries, sizeof(struct dirent *) * (*num_entries));
            if (entries == NULL) {
                perror("Failed to allocate memory");
                exit(EXIT_FAILURE);
            }
            entries[*num_entries - 1] = entry;
        }
    }
    rewinddir(dir); // reset directory stream to the beginning
    return entries;
}
time_t getLastModificationTime(char *path) {
    struct stat fileStat;
    if (stat(path, &fileStat) == -1) {
        perror("stat");
        return -1;
    }

    // Uzyskujemy datę ostatniej modyfikacji pliku
    time_t mod_time = fileStat.st_mtime;
    return mod_time;

}

char *getFullPath(const char *dirPath, const char *fileName) {
    // Uzyskujemy długość pełnej ścieżki
    size_t len_dir = strlen(dirPath);
    size_t len_file = strlen(fileName);
    size_t len_total = len_dir + len_file + 2; // 1 na '/' i 1 na '\0'

    // Alokujemy pamięć na pełną ścieżkę
    char *fullPath = (char *)malloc(len_total);
    if (fullPath == NULL) {
        perror("Memory allocation failed");
        exit(EXIT_FAILURE);
    }

    // Kopiujemy ścieżkę katalogu i nazwę pliku do pełnej ścieżki
    strcpy(fullPath, dirPath);
    fullPath[len_dir] = '/';
    strcpy(fullPath + len_dir + 1, fileName);
    
    return fullPath;
}
void deleteDirectoryRecursively(const char *dirPath) {
    DIR *dir;
    struct dirent *entry;
    

    // Otwieramy katalog
    dir = opendir(dirPath);
    if (dir == NULL) {
        perror("Unable to open directory");
        return;
    }

    // Iterujemy przez zawartość katalogu
    while ((entry = readdir(dir)) != NULL) {
        // Pomijamy "." i ".."
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        char *fullPath;
        // Tworzymy pełną ścieżkę do aktualnego elementu w katalogu
        if (entry->d_type == DT_DIR) {
            fullPath = getFullPath(dirPath, entry->d_name);
        }
        else
        {
            fullPath = getFullPath(dirPath, entry->d_name);
        }
        
        // Jeśli aktualny element jest katalogiem, wywołujemy funkcję rekurencyjnie
        if (entry->d_type == DT_DIR) {
            deleteDirectoryRecursively(fullPath);
        } else { // W przeciwnym razie usuwamy plik
            if (unlink(fullPath) == -1) {
                perror("Unable to delete file");
                free(fullPath);
                closedir(dir);
                return;
            }
        }

        // Zwolnienie pamięci zaalokowanej dla pełnej ścieżki
        free(fullPath);
    }

    // Zamykamy katalog
    closedir(dir);

    // Usuwamy sam katalog
    if (rmdir(dirPath) == -1) {
        perror("Unable to delete directory");
    }
}
void copyFile(const char *sourcePath, const char *destinationPath,int copySize)
{
    struct stat sourceStat;
    if (stat(sourcePath, &sourceStat) == -1) {
        perror("Failed to get source file stats");
        return;
    }
    off_t fileSize = sourceStat.st_size;
    if(fileSize <= copySize)
    {
        copyFileUsingRead(sourcePath,destinationPath);
    }else
    {
        copyFileUsingMmap(sourcePath,destinationPath);
    }

}

void copyFileUsingRead(const char *sourcePath, const char *destinationPath) {
    // Otwieranie pliku źródłowego do odczytu
    int sourceFile = open(sourcePath, O_RDONLY);
    if (sourceFile == -1) {
        perror("Failed to open source file");
        return;
    }

    // Otwieranie pliku docelowego do zapisu
    int destinationFile = open(destinationPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (destinationFile == -1) {
        perror("Failed to open destination file");
        return;
    }

    // Bufor do odczytu i zapisu
    char buffer[BUFFER_SIZE];

    ssize_t bytesRead, bytesWritten;
    while ((bytesRead = read(sourceFile, buffer, BUFFER_SIZE)) > 0) {
        bytesWritten = write(destinationFile, buffer, bytesRead);
        if (bytesWritten != bytesRead) {
            perror("Error while writing to destination file");
            return;
        }
    }

    // Sprawdzanie błędów przy odczycie
    if (bytesRead == -1) {
        perror("Error while reading from source file");
        return;
    }

    // Zamykanie plików
    if (close(sourceFile) == -1) {
        perror("Error while closing source file");
        return;
    }

    if (close(destinationFile) == -1) {
        perror("Error while closing destination file");
        return;
    }
}void copyFileUsingMmap(const char *sourcePath, const char *destinationPath) {
    // Otwarcie pliku źródłowego do odczytu
    int sourceFile = open(sourcePath, O_RDONLY);
    if (sourceFile == -1) {
        perror("Failed to open source file");
        return;
    }

    // Uzyskanie rozmiaru pliku źródłowego
    struct stat sourceStat;
    if (fstat(sourceFile, &sourceStat) == -1) {
        perror("Failed to get source file stats");
        close(sourceFile);
        return;
    }
    off_t fileSize = sourceStat.st_size;

    // Otwarcie pliku docelowego do zapisu
    int destinationFile = open(destinationPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (destinationFile == -1) {
        perror("Failed to open destination file");
        close(sourceFile);
        return;
    }

    // Zmapowanie pliku źródłowego do pamięci
    void *sourcePtr = mmap(NULL, fileSize, PROT_READ, MAP_PRIVATE, sourceFile, 0);
    if (sourcePtr == MAP_FAILED) {
        perror("Failed to map source file to memory");
        close(sourceFile);
        close(destinationFile);
        return;
    }

    // Zapisanie zmapowanego pliku źródłowego do pliku docelowego
    if (write(destinationFile, sourcePtr, fileSize) == -1) {
        perror("Failed to write to destination file");
    }

    // Odmapowanie pliku źródłowego z pamięci
    if (munmap(sourcePtr, fileSize) == -1) {
        perror("Failed to unmap source file from memory");
    }

    // Zamknięcie plików
    close(sourceFile);
    close(destinationFile);
}
int setFileModificationTime(const char *filePath, time_t modificationTime) {
    struct utimbuf new_times;

    // Ustawienie nowych czasów modyfikacji
    new_times.actime = time(NULL); // Zostaw aktualny czas dostępu bez zmian
    new_times.modtime = modificationTime;

    // Ustawienie czasów modyfikacji pliku
    if (utime(filePath, &new_times) == -1) {
        perror("Failed to set file modification time");
        return 0; // Zwraca 0 w przypadku błędu
    }

    return 1; // Zwraca 1 w przypadku powodzenia
}
void SyncDirNormalFiles(char *sourceDirPath,char *destinationDirPath, int copySize)
{
    DIR *sourceDir = opendir(sourceDirPath);
    if(sourceDir == NULL) {
        return;
    }
    DIR *destinationDir = opendir(destinationDirPath);
    if(destinationDir == NULL) {
        closedir(sourceDir);
        return;
    }
    int num_source_entries;
    int num_destination_entries;

    struct dirent **sourceEntries = readNormalFiles(sourceDir, &num_source_entries);
    struct dirent **destinationEntries = readNormalFiles(destinationDir, &num_destination_entries);
    for(int i = 0; i< num_source_entries;i++)
    {
        bool found = false;
        for(int j = 0;j<num_destination_entries;j++)
        {
            if(strcmp(sourceEntries[i]->d_name,destinationEntries[j]->d_name) == 0)
            {
                found = true;
                char* sourceFullPath = getFullPath(sourceDirPath, sourceEntries[i]->d_name);
                char* destinationFullPath = getFullPath(destinationDirPath, destinationEntries[j]->d_name);
                time_t sourceModTime = getLastModificationTime(sourceFullPath);
                time_t destinationModTime = getLastModificationTime(destinationFullPath);
                if(sourceModTime != destinationModTime)
                {
                    copyFile(sourceFullPath,destinationFullPath,copySize);
                    setFileModificationTime(destinationFullPath,sourceModTime);
                    printf("Kopiowanie pliku %s z %s do %s\n",sourceEntries[i]->d_name,sourceDirPath,destinationDirPath);
                }
                free(sourceFullPath);
                free(destinationFullPath);
                break;
            }
        }
        if(found==false)
        {
            char* sourceFullPath = getFullPath(sourceDirPath, sourceEntries[i]->d_name);
            char* destinationFullPath = getFullPath(destinationDirPath, sourceEntries[i]->d_name);
            copyFile(sourceFullPath,destinationFullPath,copySize);
            time_t sourceModTime = getLastModificationTime(sourceFullPath);
            setFileModificationTime(destinationFullPath,sourceModTime);
            printf("Kopiowanie pliku %s z %s do %s\n",sourceEntries[i]->d_name,sourceDirPath,destinationDirPath);
            free(sourceFullPath);
            free(destinationFullPath);
        
        }
    }
    for(int i = 0; i<num_destination_entries;i++)
    {
        bool found = false;
        for(int j = 0;j<num_source_entries;j++)
        {
            if(strcmp(destinationEntries[i]->d_name,sourceEntries[j]->d_name) == 0)
            {
                found = true;
                break;
            }
        }
        if(found == false)
        {
            char* destinationFullPath = getFullPath(destinationDirPath, destinationEntries[i]->d_name);
            deleteFile(destinationFullPath);
            printf("Usuwanie pliku %s z %s\n",destinationEntries[i]->d_name,destinationDirPath);
            free(destinationFullPath);
        }
    }

    free(sourceEntries);
    free(destinationEntries);

    closedir(sourceDir);
    closedir(destinationDir);
    return;
    
}
void SyncDirRecoursively(char *sourceDirPath,char *destinationDirPath, int copySize)
{
    DIR *sourceDir = opendir(sourceDirPath);
    if(sourceDir == NULL) {
        return;
    }
    DIR *destinationDir = opendir(destinationDirPath);
    if(destinationDir == NULL) {
        closedir(sourceDir);
        return;
    }
    int num_source_entries_dirs;
    int num_destination_entries_dirs;

    struct dirent **sourceEntriesDirs = readDirs(sourceDir, &num_source_entries_dirs);
    struct dirent **destinationEntriesDirs = readDirs(destinationDir, &num_destination_entries_dirs);

    /*int num_source_entries_files;
    int num_destination_entries_files;

    struct dirent **sourceEntriesFiles = readDirs(sourceDir, &num_source_entries_files);
    struct dirent **destinationEntriesFiles = readDirs(destinationDir, &num_destination_entries_files);*/
    for(int i = 0; i< num_source_entries_dirs;i++)
    {
        bool found = false;
        for(int j = 0;j<num_destination_entries_dirs;j++)
        {
            if(strcmp(sourceEntriesDirs[i]->d_name,destinationEntriesDirs[j]->d_name) == 0)
            {
                found = true;
                char* sourceFullPath = getFullPath(sourceDirPath, sourceEntriesDirs[i]->d_name);
                char* destinationFullPath = getFullPath(destinationDirPath, destinationEntriesDirs[j]->d_name);
                
                SyncDirRecoursively(sourceFullPath,destinationFullPath,copySize);   
                    
                printf("Kopiowanie katalogu %s z %s do %s\n",sourceEntriesDirs[i]->d_name,sourceDirPath,destinationDirPath);
                
                free(sourceFullPath);
                free(destinationFullPath);
                break;
            }
        }
        if(found==false)
        {
            char* sourceFullPath = getFullPath(sourceDirPath, sourceEntriesDirs[i]->d_name);
            char* destinationFullPath = getFullPath(destinationDirPath, sourceEntriesDirs[i]->d_name);
            mkdir(destinationFullPath, 0755);
            SyncDirRecoursively(sourceFullPath,destinationFullPath,copySize);
            printf("Kopiowanie katalogu %s z %s do %s\n",sourceEntriesDirs[i]->d_name,sourceDirPath,destinationDirPath);
            free(sourceFullPath);
            free(destinationFullPath);
        
        }
    }
    for(int i = 0; i<num_destination_entries_dirs;i++)
    {
        bool found = false;
        for(int j = 0;j<num_source_entries_dirs;j++)
        {
            if(strcmp(destinationEntriesDirs[i]->d_name,sourceEntriesDirs[j]->d_name) == 0)
            {
                found = true;
                break;
            }
        }
        if(found == false)
        {
            char* destinationFullPath = getFullPath(destinationDirPath, destinationEntriesDirs[i]->d_name);
            printf("Usuwanie katalogu %s z %s\n",destinationEntriesDirs[i]->d_name,destinationDirPath);
            deleteDirectoryRecursively(destinationFullPath);
            
            free(destinationFullPath);
        }
    }

    free(sourceEntriesDirs);
    free(destinationEntriesDirs);

    closedir(sourceDir);
    closedir(destinationDir);
    SyncDirNormalFiles(sourceDirPath,destinationDirPath,copySize);
    return;
    
}
void writeToSystemLog(const char* message) {
    time_t currentTime;
    struct tm* timeInfo;
    char timeString[20];

    // Get current time
    time(&currentTime);
    timeInfo = localtime(&currentTime);

    // Format time string
    strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", timeInfo);

    // Write message to system log
    openlog("DirSyncDaemon", LOG_PID, LOG_USER);
    syslog(LOG_INFO, "[%s] %s", timeString, message);
    closelog();
}
int stringToInt(const char *str) {
    // Sprawdź, czy łańcuch znaków jest pusty
    if (str == NULL || *str == '\0') {
        return -1;
    }

    // Użyj funkcji strtol do konwersji łańcucha znaków na liczbę całkowitą
    char *endptr;
    long int value = strtol(str, &endptr, 10);

    // Sprawdź, czy nie nastąpił błąd podczas konwersji
    if (endptr == str || *endptr != '\0') {
        return -1;
    }
    
    // Zwróć wynik konwersji
    return (int)value;
}
int main(int argc, char *argv[]) {
    // Sprawdź, czy podano dwa argumenty
    bool recursive = false;
    bool sleep = false;
    int sleepTime = 300;
    bool size = false;
    int copySize = 1024;
    if (argc < 3 ) {
        printf("deamon sourceDir destinationDir <options>\n");
        return 1;
    }

    DIR *sourceDir = opendir(argv[1]);
    if(sourceDir == NULL) {
        printf("Błąd: Podana ścieżka źródłowa nie jest katalogiem.\n");
        return 1;
    }
    closedir(sourceDir);
    DIR *destinationDir = opendir(argv[2]);
    if(destinationDir == NULL) {
        printf("Błąd: Podana ścieżka docelowa nie jest katalogiem.\n");
        return 1;     
    }
    closedir(destinationDir);
    for(int i =3;i<argc;i++)
    {
        if(strcmp(argv[i],"-R") == 0)
        {
            recursive = true;
            
        }else if(strcmp(argv[i],"-sleep") == 0)
        {
            sleep = true;
            i++;
            sleepTime = stringToInt(argv[i]);
            if(sleepTime == -1)
            {
                printf("Błąd: Podany czas spania nie jest liczbą całkowitą.\n");
                return 1;
            }

        }else if(strcmp(argv[i],"-size") == 0)
        {
            size = true;
            i++;
            copySize = stringToInt(argv[i]);
            if(size == -1)
            {
                printf("Błąd: Podany rozmiar pliku nie jest liczbą całkowitą.\n");
                return 1;
            }
        }
        else 
        {
            printf("Błąd: Nieznana opcja %s\n",argv[i]);
            return 1;
        }
    }
    if(recursive == true)
    {
        printf("Synchronizacja rekurencyjna\n");
        SyncDirRecoursively(argv[1],argv[2],-1);
    }
    if(sleep == true)
    {
        printf("Czas spania: %d\n",sleepTime);
    }
    if(size == true)
    {
        printf("Rozmiar pliku: %d\n",copySize);
    }
    else
    {
        SyncDirNormalFiles(argv[1],argv[2],-1);
    }
    
    return 0;
}

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
 W szczególności jeżeli demon  stwierdzi w katalogu docelowym  podkatalog którego brak w katalogu źródłowym powinien usunąć go wraz z zawartością.
b) [12p.] W zależności  od rozmiaru plików dla małych plików wykonywane jest kopiowanie przy pomocy read/write
 a w przypadku dużych przy pomocy mmap/write (plik źródłowy) zostaje zamapowany w całości w pamięci. 
 Próg dzielący pliki małe od dużych  może być przekazywany jako opcjonalny argument.

Uwagi: (a) Wszelkie operacje na plikach i tworzenie demona należy wykonywać
 przy pomocy API Linuksa a nie standardowej biblioteki języka C (b) 
  kopiowanie za każdym obudzeniem całego drzewa katalogów zostanie potraktowane jako poważny błąd
   (c) podobnie jak przerzucenie części zadań na shell systemowy (funkcja system).*/
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>


void copyFile(const char* srcPath, const char* destPath, size_t bufferSize) {
    int srcFile = open(srcPath, O_RDONLY);
    if (srcFile == -1) {
        printf("Błąd: Nie można otworzyć pliku źródłowego.\n");
        return;
    }

    int destFile = open(destPath, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (destFile == -1) {
        printf("Błąd: Nie można otworzyć pliku docelowego.\n");
        close(srcFile);
        return;
    }

    char buffer[bufferSize];
    ssize_t bytesRead;
    while ((bytesRead = read(srcFile, buffer, sizeof(buffer))) > 0) {
        ssize_t bytesWritten = write(destFile, buffer, bytesRead);
        if (bytesWritten == -1) {
            printf("Błąd: Nie można zapisać danych do pliku docelowego.\n");
            close(srcFile);
            close(destFile);
            return;
        }
    }

    if (bytesRead == -1) {
        printf("Błąd: Nie można odczytać danych z pliku źródłowego.\n");
    }

    close(srcFile);
    close(destFile);
}
void showDirFiles(const char* path) {
    DIR* dir = opendir(path);
    if (dir == NULL) {
        printf("Błąd: Nie można otworzyć katalogu.\n");
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            printf("Plik: %s\n", entry->d_name);
        }
    }

    closedir(dir);
}

int main(int argc, char *argv[]) {
    // Sprawdź, czy podano dwa argumenty
    if (argc != 3) {
        printf("Błąd: Należy podać dokładnie dwa argumenty - ścieżki do katalogów.\n");
        return 1;
    }

    // Sprawdź, czy pierwszy argument jest katalogiem
    struct stat dir1_stat;
    if (stat(argv[1], &dir1_stat) != 0 || !S_ISDIR(dir1_stat.st_mode)) {
        printf("Błąd: Pierwszy argument nie jest katalogiem.\n");
        return 1;
    }

    // Sprawdź, czy drugi argument jest katalogiem
    struct stat dir2_stat;
    if (stat(argv[2], &dir2_stat) != 0 || !S_ISDIR(dir2_stat.st_mode)) {
        printf("Błąd: Drugi argument nie jest katalogiem.\n");
        return 1;
    }

    // Jeżeli oba argumenty są katalogami, wypisz komunikat sukcesu
    printf("Oba argumenty są katalogami.\n");

    showDirFiles(argv[1]);
    return 0;
}
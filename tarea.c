// Giancarlo Ferretto
// 20.688.415-0
// https://github.com/Giancarlo-Ferretto/Tarea-Hilos-Semaforos

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <curl/curl.h>

#define MAX_SITE_NAME (64)

int config_cantidad_spiders;
int config_tiempo;

bool fin_tiempo_programa;

FILE *archivo_sitios;

sem_t semaforo_sitios;
sem_t semaforo_curl;
sem_t semaforo_visitados;

// Estructura auxiliar para guardar la cadena de texto con el html
//  y el tamaño que esta tiene
typedef struct mem
{
    char *memory;
    size_t size;
} mem;

// Función que es llamada por curl cada vez que recibe un bloque de datos desde la url
size_t write_callback(void *contenido, size_t size, size_t nmemb, void *userp)
{
    // Calculamos el tamaño del bloque
    size_t tamanioreal = size * nmemb;
    // Recuperamos el puntero a la memoria donde dijimos que íbamos a dejar todo
    mem *memoria = (mem *)userp;

    // Intentamos extender el tamaño de la memoria al tamaño actual + tamaño del bloque nuevo que entra
    char *ptr = realloc(memoria->memory, memoria->size + tamanioreal + 1);

    // Si retorna null, entonces no hay memoria y esto falló
    if (ptr != NULL)
    {
        // Si hay memoria, re ajustamos los punteros y copiamos el contenido del nuevo
        //   bloque al final del bloque anterior
        memoria->memory = ptr;
        memcpy(&(memoria->memory[memoria->size]), contenido, tamanioreal);
        memoria->size += tamanioreal;
        memoria->memory[memoria->size] = 0;

        // Retornamos el tamaño del bloque recibido
        return tamanioreal;
    }
    else 
    {
        fprintf(stderr, "Error: No queda memoria en write_callback.");
        return 0;
    }
}

// Función que utiliza curl.h para extraer el html de una página y guardarlo en memoria
mem *fetch_url(char *url)
{
    // Inicializaciones básicas
    CURLcode res;
    mem *memoria = (mem *)malloc(sizeof(mem));
    // Esto se irá ajustando de acuerdo a cuánto necesite
    memoria->memory = malloc(1);
    memoria->size = 0;

    // Sección crítica: Uso de la librería curl.
    sem_wait(&semaforo_curl);

    CURL* curl = curl_easy_init();
    if (!curl)
    {
        fprintf(stderr, "Error: No se pudo inicializar el curl.\n");
        return memoria;
    }

    // Se inicializa la petición de curl con la url a pedir
    curl_easy_setopt(curl, CURLOPT_URL, url);

    curl_easy_setopt( curl, CURLOPT_NOSIGNAL, 0);

    // Se establece que cada vez que se recibe un bloque de datos desde la url,
    //  se llama a la función write_callback
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    // El contenido lo escribiermos sobre la variable memoria
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)memoria);

    // Algunas páginas exigen que se identifiquen, decimos que estamos usando curl
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");

    // Ejecutamos la petición
    res = curl_easy_perform(curl);

    sem_post(&semaforo_curl); 

    // Si la petición falla, imprimimos el error.
    if (res != CURLE_OK)
    {
        fprintf(stderr, "curl_easy_perform() falla: %s\n", curl_easy_strerror(res));
    }

    // Retornamos el contenido html
    return memoria;
}

// Araña de un webcrawler.
//  Esta sólo extrae los enlaces y los imprime.
void *spider(void *data)
{
    char *url = (char *)data;

    // Extrae todo el html de la url
    mem *memoria = fetch_url(url);

    // Comienza buscando el primer enlace (Asumiendo que está en una propiedad href)
    char *inicio = strstr(memoria->memory, "href=\"");
    char *final = NULL;
    int size;

    // Comprueba que inicio no sea nulo
    if(inicio == NULL)
    {
        fprintf(stderr, "memoria falla: desborde en sitio -> %s\n", url);
    }
    else
    {
        // Se va recorriendo cada propiedad href de la página
        //  y se le imprime
        do
        {
            // para quitar  ' href=" ' del string
            inicio += 6;
            // Se busca desde el inicio hasta el siguiente ", -1 para que no lo contenga
            final = strstr(inicio, "\"") - 1;

            // +2 por el \0 y el espacio extra.
            size = final - inicio + 2;
            if(size)
            {
                char *aux = (char *)malloc(sizeof(char*)*size);
                if(aux == NULL)
                {
                    fprintf(stderr, "memoria falla: desborde en malloc do-while -> %s\n", url);
                    break;
                }
                strncpy(aux, inicio, size + 1);

                // Se coloca el caracter nulo
                aux[size - 1] = 0;

                // Cuando se enlaza dentro del mismo dominio, es costumbre no colocar la url completa
                // para asegurarnos que no se recorra el mismo enlace más de una vez, debe comenzar con su dominio
                // Sección crítica: Añadir sitios visitados a "visitados.txt".
                sem_wait(&semaforo_visitados);    
                FILE *archivo_visitados = fopen("visitados.txt", "a");
                if (aux[0] == '/')
                {
                    fprintf(archivo_visitados, "%s%s\n", url, aux);
                }
                else
                {
                    fprintf(archivo_visitados, "%s\n", aux);
                }
                fclose(archivo_visitados);
                // Se libera la memoria porque un webcrawler puede requerir demasiados recursos
                if(aux != NULL) free(aux);
                sem_post(&semaforo_visitados);
                
                sleep(0.1);
            }
            // Busca el siguiente enlace
        } while ((inicio = strstr(inicio, "href=\"")) != NULL);
    }
    // Se libera la memoria del fetch_url
    //if(memoria != NULL) free(memoria);
    //if(inicio != NULL) free(inicio);
    sleep(0.1);
}

void read_config_file() {
	char conf[16];
	FILE *archivo_conf = fopen("config.txt", "r");

	fgets(conf, sizeof conf, archivo_conf);
	if(conf != NULL) config_cantidad_spiders = atoi(conf);

	fgets(conf, sizeof conf, archivo_conf);
	if(conf != NULL) config_tiempo = atoi(conf);
	return;
}

void *timer_thread(void *arg) {
    fin_tiempo_programa = false;
    sleep((long long int)arg);
    fin_tiempo_programa = true;
    // Fin del programa, marcado luego que termina el timer_thread.
    printf("Fin del programa.\n");
}

void *spider_thread(void *arg) {
    char sitio[MAX_SITE_NAME];
    while(fin_tiempo_programa != true) {
        // Sección crítica: Leer una línea del archivo "sitios.txt".
        sem_wait(&semaforo_sitios);    
        fgets(sitio, sizeof sitio, archivo_sitios);
        sem_post(&semaforo_sitios);

        // Si el sitio no es nulo, buscar...
        if(sitio != NULL) {
            // Sección crítica: Buscar si el sitio ya fue visitado en "visitados.txt".
            sem_wait(&semaforo_visitados);  
            bool sitio_existe = false;
            char sitios_visitados[MAX_SITE_NAME];
            FILE *archivo_visitados = fopen("visitados.txt", "r");
            while(fgets(sitios_visitados, sizeof sitios_visitados, archivo_visitados)) {
                if(strstr(sitios_visitados, sitio) != NULL) {
                    sitio_existe = true;
                    break;
                }
            }
            fclose(archivo_visitados);
            sem_post(&semaforo_visitados);

            // Si el sitio no fue visitado, visitar...
            if(sitio_existe == false) {
                sitio[strcspn(sitio, "\n")] = 0;
                
                // Sección crítica: Se agrega el sitio a "visitados.txt"
                sem_wait(&semaforo_visitados);    
                FILE *archivo_visitados = fopen("visitados.txt", "a");
                fprintf(archivo_visitados, "%s\n", sitio);
                fclose(archivo_visitados);
                sem_post(&semaforo_visitados);

                // Se visita...
                printf("-> Hilo %d visitando %s...\n", (long long int)arg, sitio);
                spider(sitio);
            }
            else {
                sleep(0.1);
            }
        }
        else {
            sleep(0.1);
        }
    }
}

int main(int argc, char *argv) {
    // Inicializamos la librería curl
    curl_global_init(CURL_GLOBAL_ALL);

    // Leemos configuración
	read_config_file();
    printf("Inicio del programa con: [spiders: %d, tiempo: %d].\n", config_cantidad_spiders, config_tiempo);

    // Abrimos archivos sitios.txt
    archivo_sitios = fopen("sitios.txt", "r");

    // Se reestablece el archivo visitados.txt
    FILE *archivo_visitados = fopen("visitados.txt", "w");
    fclose(archivo_visitados);

    printf("Visitando sitios en 'visitados.txt'... si crashea es porque se queda sin memoria.\n");

    // Inicializamos semaforos
    sem_init(&semaforo_sitios, 0, 1);
    sem_init(&semaforo_curl, 0, 1);
    sem_init(&semaforo_visitados, 0, 1);

    // Inicializamos hilo tiempo
    pthread_t hilo_tiempo;
    pthread_create(&hilo_tiempo, NULL, &timer_thread, (void *)(long long int)config_tiempo);

    // Inicializamos hilos de spiders
    pthread_t hilo_spider[config_cantidad_spiders];

    for(int i = 0; i < config_cantidad_spiders; i++) {
        pthread_create(&hilo_spider[i], NULL, &spider_thread, (void *)(long long int)i);
        //printf("-> Hilo %d creado...\n", i+1);
    }

    // Ejecutamos los hilos
    pthread_join(hilo_tiempo, NULL);

    for(int i = 0; i < config_cantidad_spiders; i++) {
        pthread_join(hilo_spider[i], NULL);
    }

    // Fin curl.
    curl_global_cleanup();

    // Fin archivo.
    fclose(archivo_sitios);
    return 0;
}
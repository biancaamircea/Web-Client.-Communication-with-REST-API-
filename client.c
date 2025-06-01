#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "parson.h"

#define SERVER_HOST "63.32.125.183"
#define SERVER_PORT 8081
#define BUFFER 8192
#define SIZE 256

int admin_logged = 0;
char auth_session_cookie[SIZE] = "";
char auth_jwt_token[1024] = "";
char admin_cookie[SIZE] = "";

char *send_http_request_with_jw(const char *method, const char *url, const char *body);
char *compute_delete_request(const char *host, const char *url, const char *cookie);

int open_connection(const char *host_ip, int port) {
    int sockfd;
    struct sockaddr_in serv_addr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        exit(1);
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    inet_aton(host_ip, &serv_addr.sin_addr);

    if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        exit(1);
    }

    return sockfd;
}

void send_to_server(int sockfd, const char *message) {
    int total = strlen(message);
    int sent = 0;
    while (sent < total) {
        int bytes = send(sockfd, message + sent, total - sent, 0);
        if (bytes < 0) {
            exit(1);
        }
        sent += bytes;
    }
}

char *receive_from_server(int sockfd) {
    char buffer[BUFFER];
    char *response = calloc(1, sizeof(char));  
    int total = 0;
    int bytes;

    while ((bytes = recv(sockfd, buffer, BUFFER - 1, 0)) > 0) {
        buffer[bytes] = '\0';
        response = realloc(response, total + bytes + 1);
        strcat(response, buffer);
        total += bytes;
    }

    if (bytes < 0) {
        exit(1);
    }

    return response;
}

char *extract_body(const char *response) {
    const char *body_start = strstr(response, "\r\n\r\n");
    if (!body_start) return NULL;
    body_start += 4;
    return strdup(body_start);
}

char *compute_post_request(const char *host, const char *url, const char *content_type,
                           const char *body_data, const char *cookie) {
    char *message = calloc(BUFFER, sizeof(char));
    if (cookie && strlen(cookie) > 0) {
        sprintf(message,
            "POST %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %ld\r\n"
            "Cookie: %s\r\n"
            "Connection: close\r\n\r\n"
            "%s",
            url, host, content_type, strlen(body_data), cookie, body_data);
    } else {
        sprintf(message,
            "POST %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %ld\r\n"
            "Connection: close\r\n\r\n"
            "%s",
            url, host, content_type, strlen(body_data), body_data);
    }
    return message;
}

char *compute_get_request(const char *host, const char *url, const char *cookie) {
    char *message = calloc(BUFFER, sizeof(char));
    if (cookie && strlen(cookie) > 0) {
        sprintf(message,
            "GET %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Cookie: %s\r\n"
            "Connection: close\r\n\r\n",
            url, host, cookie);
    } else {
        sprintf(message,
            "GET %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Connection: close\r\n\r\n",
            url, host);
    }
    return message;
}

void extract_cookie(char *response, char *cookie_dest) {
    char *cookie_start = strstr(response, "Set-Cookie: ");
    if (cookie_start) {
        cookie_start += strlen("Set-Cookie: ");
        char *cookie_end = strchr(cookie_start, ';');
        if (cookie_end) *cookie_end = '\0';
        snprintf(cookie_dest, 256, "%s", cookie_start);
    } else {
        cookie_dest[0] = '\0';
    }
}

void input(const char *label, char *buf, size_t size) {
    printf("%s=", label);
    fflush(stdout);

    int c;
    size_t i = 0;

    while (i < size - 1 && (c = getchar()) != '\n' && c != EOF) {
        buf[i++] = (char)c;
    }
    buf[i] = '\0';

    if (c != '\n' && c != EOF) {
        while ((c = getchar()) != '\n' && c != EOF);
    }
}

int status(const char *response) {
    int code = 0;
    const char *ptr = strstr(response, "HTTP/1.1 ");
    if (ptr) {
        ptr += 9; 
        
        if (sscanf(ptr, "%d", &code) == 1) {
            return code;
        }
    }
    return 0;
}

char *request(const char *method, const char *url, const char *body, int use_cookie) {
    int sockfd = open_connection(SERVER_HOST, SERVER_PORT);

    char *request = NULL;

    if (strcmp(method, "GET") == 0) {
        request = compute_get_request(SERVER_HOST, url, use_cookie ? auth_session_cookie : NULL);
    } else if (strcmp(method, "POST") == 0) {
        request = compute_post_request(SERVER_HOST, url, "application/json", body ? body : "", use_cookie ? auth_session_cookie : NULL);
    } else if (strcmp(method, "DELETE") == 0) {
        request = compute_delete_request(SERVER_HOST, url, use_cookie ? auth_session_cookie : NULL);
    } else {
        puts("FAIL: Metodă HTTP necunoscută.");
        return NULL;
    }

    send_to_server(sockfd, request);
    char *response = receive_from_server(sockfd);

    close(sockfd);
    free(request);
    return response;
}

char *compute_delete_request(const char *host, const char *url, const char *cookie) {
    char *message = calloc(BUFFER, sizeof(char));

    sprintf(message,
        "DELETE %s HTTP/1.1\r\n"
        "Host: %s\r\n",
        url, host);

    if (cookie != NULL && strlen(cookie) > 0) {
        strcat(message, "Cookie: ");
        strcat(message, cookie);
        strcat(message, "\r\n");
    }

    strcat(message, "Connection: close\r\n\r\n");
    return message;
}

char *send_http_request_with_jw(const char *method, const char *url, const char *body) {
    int sockfd = open_connection(SERVER_HOST, SERVER_PORT);

    char *message = calloc(BUFFER, sizeof(char));
    sprintf(message,
        "%s %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Authorization: Bearer %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n\r\n"
        "%s",
        method, url, SERVER_HOST, auth_jwt_token, strlen(body), body);

    send_to_server(sockfd, message);
    char *response = receive_from_server(sockfd);

    close(sockfd);
    free(message);
    return response;
} 

void login_admin() {
    char username[100], password[100];

    printf("username=");
    fflush(stdout);
    if (fgets(username, sizeof(username), stdin) != NULL) {
        username[strcspn(username, "\n")] = '\0';
    }

    printf("password=");
    fflush(stdout);
    if (fgets(password, sizeof(password), stdin) != NULL) {
        password[strcspn(password, "\n")] = '\0';
    }

    JSON_Value *val = json_value_init_object();
    JSON_Object *obj = json_value_get_object(val);
    json_object_set_string(obj, "username", username);
    json_object_set_string(obj, "password", password);
    char *data = json_serialize_to_string(val);

    int sockfd = open_connection(SERVER_HOST, SERVER_PORT);
    if (sockfd < 0) {
        fprintf(stderr, "Eroare la deschiderea conexiunii\n");
        json_free_serialized_string(data);
        json_value_free(val);
        return;
    }

    char *req = compute_post_request(SERVER_HOST, "/api/v1/tema/admin/login", "application/json", data, NULL);
    send_to_server(sockfd, req);

    char *resp = receive_from_server(sockfd);

    int code = status(resp);
    if (code == 200) {
        printf("SUCCESS: Admin logat cu succes!\n");

        extract_cookie(resp, admin_cookie);
        if (strlen(admin_cookie) > 0) {
            strcpy(auth_session_cookie, admin_cookie);
            admin_logged = 1;
        }
    } else {
        printf("ERROR: Autentificare esuata pentru admin\n");
    }

    free(req);
    free(resp);
    json_free_serialized_string(data);
    json_value_free(val);
    close(sockfd);
}

void logout_admin() {
    if (strlen(auth_session_cookie) == 0) {
        return;
    }

    char *logout_response = request("GET", "/api/v1/tema/admin/logout", NULL, 1);

    if (!logout_response) {
        fputs("EȘEC: Nu s-a primit niciun răspuns de la server.\n", stdout);
        return;
    }

    char *confirmation = strstr(logout_response, "logged out");
    char *cookie_reset = strstr(logout_response, "Set-Cookie");

    int success = confirmation || cookie_reset || strstr(logout_response, "success");

    if (success) {
        memset(auth_session_cookie, 0, sizeof(auth_session_cookie));
        
    } else {
        puts("EȘEC: Serverul nu a confirmat delogarea.");
    }

    free(logout_response);
}


void login() {
    char admin_user[100], user[100], pass[100];
    
    printf("admin_username=");
    fflush(stdout);
    fgets(admin_user, sizeof(admin_user), stdin);
    admin_user[strcspn(admin_user, "\n")] = '\0';

    printf("username=");
    fflush(stdout);
    fgets(user, sizeof(user), stdin);
    user[strcspn(user, "\n")] = '\0';

    printf("password=");
    fflush(stdout);
    fgets(pass, sizeof(pass), stdin);
    pass[strcspn(pass, "\n")] = '\0';

    JSON_Value *val = json_value_init_object();
    JSON_Object *obj = json_value_get_object(val);
    if (val == NULL || obj == NULL) {
        fprintf(stderr, "Eroare la crearea obiectului JSON\n");
        return;
    }

    json_object_set_string(obj, "admin_username", admin_user);
    json_object_set_string(obj, "username", user);
    json_object_set_string(obj, "password", pass);

    char *json_str = json_serialize_to_string(val);
    if (json_str == NULL) {
        fprintf(stderr, "Eroare la serializarea JSON-ului\n");
        json_value_free(val);
        return;
    }

    int sockfd = open_connection(SERVER_HOST, SERVER_PORT);
    char *request_str = compute_post_request(SERVER_HOST, "/api/v1/tema/user/login", "application/json", json_str, NULL);
    send_to_server(sockfd, request_str);
    char *response = receive_from_server(sockfd);

    if (response && status(response) == 200) {
        extract_cookie(response, auth_session_cookie);
        puts("SUCCESS: Autentificare reușită");
    } else {
        puts("FAIL: Autentificare eșuată");
    }

    free(request_str);
    free(response);
    json_free_serialized_string(json_str);
    json_value_free(val);
    close(sockfd);
}

void logout() {
    int is_logged = strlen(auth_session_cookie) > 0;
    if (!is_logged) {
        puts("FAIL: Niciun utilizator conectat.");
        return;
    }

    const char *endpoint = "/api/v1/tema/user/logout";
    char *headers[] = { auth_session_cookie, NULL };

    char *logout_response = request("GET", endpoint, NULL, 1);
    int http_code = logout_response ? status(logout_response) : 0;

    switch (http_code) {
        case 200:
            for (int i = 0; i < sizeof(auth_session_cookie); i++) {
                auth_session_cookie[i] = 0;
            }
            puts("SUCCESS: Delogare efectuată cu succes.");
            break;
        default:
            puts("ERROR: Delogarea nu a fost procesată corect.");
            break;
    }

    free(logout_response);
}

void add_user() {
    if (!admin_logged) {
        printf("ERROR: Trebuie sa fii logat ca admin\n");
        return;
    }

    char username[100], password[100];
    printf("username="); fflush(stdout);
    fgets(username, sizeof(username), stdin);
    username[strcspn(username, "\n")] = '\0';

    printf("password="); fflush(stdout);
    fgets(password, sizeof(password), stdin);
    password[strcspn(password, "\n")] = '\0';

    JSON_Value *val = json_value_init_object();
    JSON_Object *obj = json_value_get_object(val);
    json_object_set_string(obj, "username", username);
    json_object_set_string(obj, "password", password);
    char *data = json_serialize_to_string(val);

    char *req = compute_post_request(SERVER_HOST, "/api/v1/tema/admin/users",
                                     "application/json", data, auth_session_cookie);
    int sockfd = open_connection(SERVER_HOST, SERVER_PORT);
    send_to_server(sockfd, req);
    char *resp = receive_from_server(sockfd);

    int code = status(resp);
    if (strstr(resp, "200 OK") || strstr(resp, "201")) {
                printf("SUCCESS: Utilizator adaugat cu succes!\n");
            } else {
                printf("ERROR: Nu s-a putut adauga utilizatorul\n");
            }

    json_free_serialized_string(data);
    json_value_free(val);
    free(req); free(resp);
    close(sockfd);
}

void add_movie() {
    if (auth_jwt_token[0] == '\0') {
        puts("ERROR: Trebuie să ceri acces cu get_access înainte.");
        return;
    }
    
    char title[100], description[300];
    int year;
    float rating;

    printf("title=");
    fgets(title, sizeof(title), stdin);
    title[strcspn(title, "\n")] = '\0';  

    printf("year=");
    if (scanf("%d", &year) != 1 || year <= 0) {
        puts("ERROR: Anul trebuie să fie un număr pozitiv.");
        return;
    }

    getchar(); 

    printf("description=");
    fgets(description, sizeof(description), stdin);
    description[strcspn(description, "\n")] = '\0';  

    printf("rating=");
    if (scanf("%f", &rating) != 1 || rating < 0 || rating > 10) {
        puts("ERROR: Ratingul trebuie să fie un număr între 0 și 10.");
        return;
    }
    int c;
while ((c = getchar()) != '\n' && c != EOF) { }
   
    char *json_body = calloc(BUFFER, sizeof(char));
    sprintf(json_body, 
        "{\"title\":\"%s\",\"year\":%d,\"description\":\"%s\",\"rating\":%.1f}",
        title, year, description, rating);

    int sockfd = open_connection(SERVER_HOST, SERVER_PORT);
    char *message = calloc(BUFFER, sizeof(char));
    sprintf(message,
        "POST /api/v1/tema/library/movies HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Authorization: Bearer %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n\r\n"
        "%s",
        SERVER_HOST, auth_jwt_token, strlen(json_body), json_body);

    send_to_server(sockfd, message);
    char *resp = receive_from_server(sockfd);

    if (!resp) {
        puts("FAIL: Nu s-a primit niciun răspuns de la server.");
        free(json_body);
        free(message);
        close(sockfd);
        return;
    }

    int code = status(resp);
    if (code == 200 || code == 201) {
        puts("SUCCESS: Filmul a fost adăugat.");
    } else {
        printf("FAIL: Nu s-a putut adăuga filmul. Cod eroare: %d\n", code);
    }

    free(json_body);
    free(message);
    free(resp);
    close(sockfd);
}

void get_movies() {
    if (auth_jwt_token[0] == '\0') {
        puts("ERROR: Trebuie să ceri acces cu get_access înainte.");
        return;
    }

    int sockfd = open_connection(SERVER_HOST, SERVER_PORT);
    char *message = calloc(BUFFER, sizeof(char));
    sprintf(message,
        "GET /api/v1/tema/library/movies HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Authorization: Bearer %s\r\n"
        "Connection: close\r\n\r\n",
        SERVER_HOST, auth_jwt_token);

    send_to_server(sockfd, message);
    char *resp = receive_from_server(sockfd);

    if (!resp) {
        puts("ERROR: Nu s-a primit niciun răspuns de la server.");
        free(message);
        close(sockfd);
        return;
    }

    int code = status(resp);

    if (code == 401) {
        puts("ERROR: Acces neautorizat. Trebuie să obții tokenul cu get_access.");
        free(message);
        free(resp);
        close(sockfd);
        return;
    }

    if (code != 200) {
        printf("ERROR: Cererea a eșuat cu codul %d.\n", code);
        free(message);
        free(resp);
        close(sockfd);
        return;
    }

    const char *body = strstr(resp, "\r\n\r\n");
    if (!body) {
        puts("ERROR: Răspuns invalid de la server.");
        free(message);
        free(resp);
        close(sockfd);
        return;
    }
    body += 4;  

    JSON_Value *root_value = json_parse_string(body);
    if (!root_value) {
        puts("ERROR: JSON invalid.");
        free(message);
        free(resp);
        close(sockfd);
        return;
    }

    JSON_Array *movies = NULL;
    if (json_value_get_type(root_value) == JSONArray) {
        movies = json_value_get_array(root_value);
    } else {
        JSON_Object *root_obj = json_value_get_object(root_value);
        if (root_obj) {
            movies = json_object_get_array(root_obj, "movies");
        }
    }

    if (!movies) {
        puts("ERROR: Nu s-a găsit lista de filme.");
        json_value_free(root_value);
        free(message);
        free(resp);
        close(sockfd);
        return;
    }

    size_t count = json_array_get_count(movies);
    if (count == 0) {
        puts("INFO: Nu există filme disponibile.");
    } else {
        puts("SUCCESS: Lista filmelor:");
        for (size_t i = 0; i < count; i++) {
            JSON_Object *movie = json_array_get_object(movies, i);
            int id = (int)json_object_get_number(movie, "id");
            const char *title = json_object_get_string(movie, "title");
            if (title) {
                printf("#%d %s\n", id, title);
            } else {
                printf("#%d (no title)\n", id);
            }
        }
    }

    json_value_free(root_value);
    free(message);
    free(resp);
    close(sockfd);
}

void update_movie() {
    if (auth_jwt_token[0] == '\0') {
        puts("ERROR: Trebuie să ceri acces cu get_access înainte.");
        return;
    }

    int movieId;
    char title[100], description[300];
    int year;
    float rating;

    printf("id=");
    if (scanf("%d", &movieId) != 1 || movieId <= 0) {
        puts("ERROR: ID invalid.");
        return;
    }
    int c;
    while ((c = getchar()) != '\n' && c != EOF) {}

    printf("title=");
    fgets(title, sizeof(title), stdin);
    title[strcspn(title, "\n")] = '\0';

    printf("year=");
    if (scanf("%d", &year) != 1 || year <= 0) {
        puts("ERROR: Anul trebuie să fie un număr pozitiv.");
        return;
    }
    while ((c = getchar()) != '\n' && c != EOF) {}

    printf("description=");
    fgets(description, sizeof(description), stdin);
    description[strcspn(description, "\n")] = '\0';

    printf("rating=");
    if (scanf("%f", &rating) != 1 || rating < 0 || rating > 10) {
        puts("ERROR: Ratingul trebuie să fie un număr între 0 și 10.");
        return;
    }
    while ((c = getchar()) != '\n' && c != EOF) {}

    char *json_body = calloc(BUFFER, sizeof(char));
    sprintf(json_body, 
        "{\"id\":%d,\"title\":\"%s\",\"year\":%d,\"description\":\"%s\",\"rating\":%.1f}",
        movieId, title, year, description, rating);

    char endpoint[150];
    sprintf(endpoint, "/api/v1/tema/library/movies/%d", movieId);

    int sockfd = open_connection(SERVER_HOST, SERVER_PORT);
    char *message = calloc(BUFFER, sizeof(char));
    sprintf(message,
        "PUT %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Authorization: Bearer %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n\r\n"
        "%s",
        endpoint, SERVER_HOST, auth_jwt_token, strlen(json_body), json_body);

    send_to_server(sockfd, message);
    char *resp = receive_from_server(sockfd);

    if (!resp) {
        puts("ERROR: Nu s-a primit niciun răspuns de la server.");
        free(json_body);
        free(message);
        close(sockfd);
        return;
    }

    int code = status(resp);
    switch (code) {
        case 200:
        case 204:
            puts("SUCCESS: Filmul a fost actualizat.");
            break;
        case 401:
            puts("ERROR: Acces neautorizat. Trebuie să obții tokenul cu get_access.");
            break;
        case 404:
            puts("ERROR: Filmul cu ID-ul specificat nu a fost găsit.");
            break;
        case 400:
            puts("ERROR: Date invalide sau incomplete.");
            break;
        default:
            printf("FAIL: Actualizarea a eșuat. Cod eroare: %d\n", code);
            break;
    }


    free(json_body);
    free(message);
    free(resp);
    close(sockfd);
}

void delete_movie() {
    if (auth_jwt_token[0] == '\0') {
        puts("ERROR: Trebuie să ceri acces cu get_access înainte.");
        return;
    }

    int movieId;
    printf("id=");
    if (scanf("%d", &movieId) != 1 || movieId <= 0) {
        puts("ERROR: ID invalid.");
        return;
    }
    int c;
    while ((c = getchar()) != '\n' && c != EOF) {}

    char endpoint[150];
    sprintf(endpoint, "/api/v1/tema/library/movies/%d", movieId);

    int sockfd = open_connection(SERVER_HOST, SERVER_PORT);
    char *message = calloc(BUFFER, sizeof(char));
    sprintf(message,
        "DELETE %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Authorization: Bearer %s\r\n"
        "Connection: close\r\n\r\n",
        endpoint, SERVER_HOST, auth_jwt_token);

    send_to_server(sockfd, message);
    char *resp = receive_from_server(sockfd);

    if (!resp) {
        puts("ERROR: Nu s-a primit niciun răspuns de la server.");
        free(message);
        close(sockfd);
        return;
    }

    int code = status(resp);
    if (code == 200 || code == 204) {
        puts("SUCCESS: Filmul a fost șters.");
    } else if (code == 401) {
        puts("ERROR: Acces neautorizat. Trebuie să obții tokenul cu get_access.");
    } else if (code == 404) {
        puts("ERROR: Filmul cu ID-ul specificat nu a fost găsit.");
    } else if (code == 400) {
        puts("ERROR: ID invalid.");
    } else {
        printf("FAIL: Ștergerea a eșuat. Cod eroare: %d\n", code);
    }

    free(message);
    free(resp);
    close(sockfd);
}

void get_access() {
    if (auth_session_cookie[0] == '\0') {
        puts("ERROR: Nu ești autentificat.");
        return;
    }

    char *resp = request("GET", "/api/v1/tema/library/access", NULL, 1);
    if (!resp) {
        return;
    }

    int code = status(resp);
    if (code != 200) {
        free(resp);
        return;
    }

    const char *body = strstr(resp, "\r\n\r\n");
    if (!body) {
        free(resp);
        return;
    }
    body += 4;

    JSON_Value *root = json_parse_string(body);
    if (!root) {
        puts("FAIL: JSON invalid.");
        free(resp);
        return;
    }

    JSON_Object *obj = json_value_get_object(root);
    const char *token = json_object_get_string(obj, "token");

    if (token) {
         strncpy(auth_jwt_token, token, SIZE
         - 1);
    auth_jwt_token[SIZE - 1] = '\0';  
    printf("SUCCESS: Token JWT primit\n");
        printf("DEBUG: Token stocat in auth_jwt_token: '%s'\n", auth_jwt_token);

    } else {
        puts("FAIL: Tokenul nu a fost găsit.");
    }

    json_value_free(root);
    free(resp);
}

void get_users() {
    if (auth_session_cookie[0] == '\0') {
        puts("ERROR: Not authenticated as admin");
        return;
    }

    char request[BUFFER];
    snprintf(request, sizeof(request),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Cookie: %s\r\n"
        "Connection: close\r\n\r\n",
        "/api/v1/tema/admin/users", SERVER_HOST, auth_session_cookie);

    int sockfd = open_connection(SERVER_HOST, SERVER_PORT);
    send_to_server(sockfd, request);
    char *response = receive_from_server(sockfd);
    close(sockfd);

    if (!response) {
        puts("FAIL: No response received from server");
        return;
    }

    
    char *body = strstr(response, "\r\n\r\n");
    if (!body) {
        puts("FAIL");
        free(response);
        return;
    }
    body += 4;  

    int code = status(response);

    if (code != 200) {
        JSON_Value *error_val = json_parse_string(body);
        if (error_val) {
            JSON_Object *error_obj = json_value_get_object(error_val);
            const char *err_msg = json_object_get_string(error_obj, "error");
            printf("ERROR: %s\n", err_msg ? err_msg : "Unknown error");
            json_value_free(error_val);
        } else {
            puts("ERROR: Unable to parse error message");
        }
        free(response);
        return;
    }


    JSON_Value *root_val = json_parse_string(body);
    if (!root_val) {
        puts("FAIL: Invalid JSON in response body");
        free(response);
        return;
    }

    JSON_Object *root_obj = json_value_get_object(root_val);
    JSON_Array *users = json_object_get_array(root_obj, "users");
    if (!users) {
        puts("FAIL: Users list missing in JSON");
        json_value_free(root_val);
        free(response);
        return;
    }

    printf("SUCCESS: Lista utilizatorilor\n");

    size_t count = json_array_get_count(users);
    for (size_t i = 0; i < count; i++) {
        JSON_Object *user = json_array_get_object(users, i);
        const char *username = json_object_get_string(user, "username");
        const char *password = json_object_get_string(user, "password");

        char uname_buf[128], pass_buf[128];
        snprintf(uname_buf, sizeof(uname_buf), "%s", username ? username : "");
        snprintf(pass_buf, sizeof(pass_buf), "%s", password ? password : "");

        printf("#%zu %s:%s\n", i + 1, uname_buf, pass_buf);
    }

    json_value_free(root_val);
    free(response);
}

void delete_user() {
    if (auth_session_cookie[0] == '\0') {
        puts("ERROR: Trebuie să fii logat ca admin.");
        return;
    }

    char user[100];
    input("username", user, sizeof(user));

    char url[256];
    snprintf(url, sizeof(url), "/api/v1/tema/admin/users/%s", user);

    char *resp = request("DELETE", url, NULL, 1);
    if (!resp) {
        puts("FAIL: Nu s-a primit răspuns.");
        return;
    }

    int code = status(resp);

    if (code == 200 || code == 204) {
        puts("SUCCESS: Utilizator șters");
    } else if (code == 404) {
        puts("FAIL: Utilizatorul nu există.");
    } else if (code == 401 || code == 403) {
        puts("ERROR: Nu ai permisiunea să ștergi acest utilizator.");
    } else if (code == 400 || code == 405) {
        puts("FAIL: Ștergerea a eșuat. Cerere invalidă sau metodă nepermisă.");
    } else {
        printf("FAIL: Ștergerea a eșuat. Cod eroare: %d\n", code);
    }

    free(resp);
}


void get_movie() {
    if (strlen(auth_jwt_token) == 0) {
        puts("ERROR: Trebuie sa ceri acces cu get_access inainte.");
        return;
    }

    char movie_id[32];
    printf("id=");
    fflush(stdout);
    if (!fgets(movie_id, sizeof(movie_id), stdin)) {
        puts("ERROR: Nu s-a putut citi ID-ul.");
        return;
    }
    movie_id[strcspn(movie_id, "\n")] = '\0';  

    if (strlen(movie_id) == 0 || atoi(movie_id) <= 0) {
        puts("ERROR: ID invalid.");
        return;
    }

    char url[256];
    snprintf(url, sizeof(url), "/api/v1/tema/library/movies/%s", movie_id);

    int sockfd = open_connection(SERVER_HOST, SERVER_PORT);
    if (sockfd < 0) {
        puts("ERROR: Nu s-a putut deschide conexiunea.");
        return;
    }

    char *message = calloc(BUFFER, sizeof(char));
    snprintf(message, BUFFER,
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Authorization: Bearer %s\r\n"
        "Connection: close\r\n\r\n",
        url, SERVER_HOST, auth_jwt_token);

    send_to_server(sockfd, message);
    free(message);

    char *response = receive_from_server(sockfd);
    close(sockfd);

    if (!response) {
        puts("ERROR: Nu s-a primit raspuns de la server.");
        return;
    }

    int status_code = status(response);

    if (status_code == 200) {
        char *body = strstr(response, "\r\n\r\n");
        if (body) {
            const char *content = body + strlen("\r\n\r\n");

            JSON_Value *parsed = json_parse_string(content);
        if (parsed == NULL) {
            fprintf(stderr, "ERROR: Nu s-a putut parsa corpul răspunsului.\n");
            free(response);
            return;
        }

        JSON_Object *film = json_value_get_object(parsed);
        if (film == NULL) {
            fprintf(stderr, "ERROR: Structura JSON este invalidă.\n");
            json_value_free(parsed);
            free(response);
            return;
        }

            const char *title = json_object_get_string(film, "title");
            int year = (int)json_object_get_number(film, "year");
            const char *description = json_object_get_string(film, "description");

            double rating = 0.0;
            JSON_Value *rating_val = json_object_get_value(film, "rating");
            if (rating_val) {
                if (json_value_get_type(rating_val) == JSONString) {
                    const char *rating_str = json_object_get_string(film, "rating");
                    if (rating_str) rating = atof(rating_str);
                } else {
                    rating = json_object_get_number(film, "rating");
                }
            }

            printf("SUCCESS: Detalii film:\n");
            printf("Title: %s\n", title ? title : "N/A");
            printf("Year: %d\n", year);
            printf("Description: %s\n", description ? description : "N/A");
            printf("Rating: %.1f\n", rating);

            json_value_free(parsed);
        } else {
            puts("ERROR: Corpul raspunsului lipseste.");
        }

    } else if (status_code == 401) {
        puts("ERROR: Acces neautorizat. Trebuie sa te autentifici.");
    } else if (status_code == 404) {
        puts("ERROR: Filmul nu a fost gasit.");
    } else {
        char *body = strstr(response, "\r\n\r\n");
        if (body) {
            body += 4;
            JSON_Value *root = json_parse_string(body);
            if (root) {
                const char *error_msg = json_object_get_string(json_value_get_object(root), "error");
                printf("ERROR: %s\n", error_msg ? error_msg : "Eroare necunoscuta");
                json_value_free(root);
            } else {
                puts("ERROR: Raspuns invalid de la server.");
            }
        } else {
            puts("ERROR: Raspuns invalid de la server.");
        }
    }

    free(response);
}

void add_movie_to_collection(int collectionId, int movieId) {
    if (auth_jwt_token[0] == '\0') {
        puts("ERROR: Trebuie să ceri acces cu get_access înainte.");
        return;
    }

    char endpoint[150];
    sprintf(endpoint, "/api/v1/tema/library/collections/%d/movies", collectionId);

    char *json_body = calloc(BUFFER, sizeof(char));
    sprintf(json_body, "{\"id\":%d}", movieId);

    int sockfd = open_connection(SERVER_HOST, SERVER_PORT);
    char *message = calloc(BUFFER, sizeof(char));
    sprintf(message,
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Authorization: Bearer %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n\r\n"
        "%s",
        endpoint, SERVER_HOST, auth_jwt_token, strlen(json_body), json_body);

    send_to_server(sockfd, message);
    char *resp = receive_from_server(sockfd);

    if (!resp) {
        puts("FAIL: Nu s-a primit niciun răspuns de la server.");
        free(json_body);
        free(message);
        close(sockfd);
        return;
    }

    int code = status(resp);
    if (code == 200 || code == 201) {
        puts("SUCCESS: Filmul a fost adăugat în colecție.");
    } else if (code == 401) {
        puts("ERROR: Acces neautorizat.");
    } else if (code == 403) {
        puts("ERROR: Nu sunteți owner al colecției.");
    } else if (code == 400) {
        puts("ERROR: Date invalide sau incomplete.");
    } else if (code == 404) {
        puts("ERROR: Colecția sau filmul nu a fost găsit.");
    } else {
        printf("FAIL: Adăugarea filmului în colecție a eșuat. Cod eroare: %d\n", code);
    }

    free(json_body);
    free(message);
    free(resp);
    close(sockfd);
}


void add_collection() {
    if (auth_jwt_token[0] == '\0') {
        puts("ERROR: Trebuie să ceri acces cu get_access înainte.");
        return;
    }

    char title[100];
    printf("title=");
    fflush(stdout);
    fgets(title, sizeof(title), stdin);
    title[strcspn(title, "\n")] = '\0';

    char *json_body = calloc(BUFFER, sizeof(char));
    sprintf(json_body, "{\"title\":\"%s\"}", title);

    int sockfd = open_connection(SERVER_HOST, SERVER_PORT);
    char *message = calloc(BUFFER, sizeof(char));
    sprintf(message,
        "POST /api/v1/tema/library/collections HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Authorization: Bearer %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n\r\n"
        "%s",
        SERVER_HOST, auth_jwt_token, strlen(json_body), json_body);

    send_to_server(sockfd, message);
    char *resp = receive_from_server(sockfd);

    free(json_body);
    free(message);
    close(sockfd);

    if (!resp) {
        puts("FAIL: Nu s-a primit niciun răspuns de la server.");
        return;
    }

    int code = status(resp);
    if (code != 200 && code != 201) {
        printf("FAIL: Crearea colecției a eșuat. Cod eroare: %d\n", code);
        free(resp);
        return;
    }

    const char *body = strstr(resp, "\r\n\r\n");
    if (body == NULL) {
        fprintf(stderr, "FAIL: Nu s-a putut găsi corpul răspunsului HTTP.\n");
        free(resp);
        return;
    }
    body += strlen("\r\n\r\n");

    JSON_Value *root_value = json_parse_string(body);
    if (root_value == NULL) {
        fprintf(stderr, "FAIL: Eroare la parsarea conținutului JSON.\n");
        free(resp);
        return;
    }   

    JSON_Object *root_obj = json_value_get_object(root_value);
    int collectionId = (int)json_object_get_number(root_obj, "id");

    printf("SUCCESS: Colecția a fost creată.\n");
    printf("Colecția are id-ul: %d\n", collectionId);

    json_value_free(root_value);
    free(resp);

    int num_movies;
    printf("num_movies=");
    fflush(stdout);
    if (scanf("%d", &num_movies) != 1 || num_movies < 0) {
        puts("ERROR: Număr invalid.");
        int c; while ((c = getchar()) != '\n' && c != EOF) {}
        return;
    }
    int c; while ((c = getchar()) != '\n' && c != EOF) {}

   for (int i = 0; i < num_movies; i++) {
    int movieId;
    printf("movie_id[%d]=", i);
    fflush(stdout);

    if (scanf("%d", &movieId) == 1) {
        int c; while ((c = getchar()) != '\n' && c != EOF) {}
        add_movie_to_collection(collectionId, movieId);
    } else {
        puts("ERROR: ID film invalid.");
        int c; while ((c = getchar()) != '\n' && c != EOF) {}
    }
}


    printf("SUCCESS: Colecție adăugată\n");
}

void get_collection() {
    if (auth_jwt_token[0] == '\0') {
        puts("ERROR: Trebuie să ceri acces cu get_access înainte.");
        return;
    }

    int collection_id;
    printf("id=");
    fflush(stdout);
    if (scanf("%d", &collection_id) != 1) {
        puts("ERROR: ID invalid.");
        int c; while ((c = getchar()) != '\n' && c != EOF) {}
        return;
    }
    int c; while ((c = getchar()) != '\n' && c != EOF) {}

    char endpoint[100];
    sprintf(endpoint, "/api/v1/tema/library/collections/%d", collection_id);

    int sockfd = open_connection(SERVER_HOST, SERVER_PORT);
    char *message = calloc(BUFFER, sizeof(char));
    sprintf(message,
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Authorization: Bearer %s\r\n"
        "Connection: close\r\n\r\n",
        endpoint, SERVER_HOST, auth_jwt_token);

    send_to_server(sockfd, message);
    char *resp = receive_from_server(sockfd);

    if (!resp) {
        puts("FAIL: Nu s-a primit niciun răspuns.");
        free(message);
        close(sockfd);
        return;
    }

    int code = status(resp);
    if (code != 200) {
        if (code == 404)
            puts("ERROR: Colecția nu a fost găsită.");
        else
            printf("FAIL: Cod eroare necunoscut: %d\n", code);
        goto cleanup;
    }

    puts("SUCCESS: Detalii colecție");

    const char *body = strstr(resp, "\r\n\r\n");
    if (!body) {
        puts("FAIL: Răspuns invalid.");
        goto cleanup;
    }
    body += 4;

    JSON_Value *val = json_parse_string(body);
    if (!val) {
        puts("FAIL: Nu s-a putut parsa JSON.");
        goto cleanup;
    }

    JSON_Object *obj = json_value_get_object(val);
    const char *title = json_object_get_string(obj, "title");
    const char *owner = json_object_get_string(obj, "owner");
    printf("title: %s\n", title);
    printf("owner: %s\n", owner);

    JSON_Array *movies = json_object_get_array(obj, "movies");
    if (movies) {
        for (size_t i = 0; i < json_array_get_count(movies); i++) {
            JSON_Object *movie = json_array_get_object(movies, i);
            int id = (int)json_object_get_number(movie, "id");
            const char *name = json_object_get_string(movie, "title");

            printf("#%d: %s\n", id, name);
        }
    }

    json_value_free(val);

    cleanup:
    free(message);
    free(resp);
    close(sockfd);
}

void delete_collection() {
    if (auth_jwt_token[0] == '\0') {
        puts("ERROR: Trebuie să ceri acces cu get_access înainte.");
        return;
    }

    int collectionId;
    printf("id=");
    fflush(stdout);
    if (scanf("%d", &collectionId) != 1) {
        puts("ERROR: ID invalid.");
        int c; while ((c = getchar()) != '\n' && c != EOF) {}
        return;
    }

    int c; while ((c = getchar()) != '\n' && c != EOF) {}

    char endpoint[100];
    sprintf(endpoint, "/api/v1/tema/library/collections/%d", collectionId);

    int sockfd = open_connection(SERVER_HOST, SERVER_PORT);

    char *message = calloc(BUFFER, sizeof(char));
    sprintf(message,
        "DELETE %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Authorization: Bearer %s\r\n"
        "Connection: close\r\n\r\n",
        endpoint, SERVER_HOST, auth_jwt_token);

    send_to_server(sockfd, message);
    char *resp = receive_from_server(sockfd);

    if (!resp) {
        puts("FAIL: Nu s-a primit niciun răspuns de la server.");
        free(message);
        close(sockfd);
        return;
    }

    int code = status(resp);
    if (code == 200 || code == 204) {
        printf("SUCCESS: Colecție ștearsă\n");
    } else if (code == 401) {
        puts("ERROR: Acces neautorizat.");
    } else if (code == 403) {
        puts("ERROR: Nu sunteți owner-ul colecției.");
    } else if (code == 404) {
        puts("ERROR: Colecția nu a fost găsită.");
    } else {
        printf("FAIL: Ștergerea colecției a eșuat. Cod eroare: %d\n", code);
    }

    free(message);
    free(resp);
    close(sockfd);
}

void get_collections() {
    if (auth_jwt_token[0] == '\0') {
        printf("ERROR: Acces refuzat. Trebuie să te autentifici cu get_access.\n");
        return;
    }

    int sockfd = open_connection(SERVER_HOST, SERVER_PORT);
    if (sockfd < 0) {
        printf("ERROR: Nu s-a putut stabili conexiunea cu serverul.\n");
        return;
    }

    char request[BUFFER];
    snprintf(request, sizeof(request),
        "GET /api/v1/tema/library/collections HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Authorization: Bearer %s\r\n"
        "Connection: close\r\n\r\n",
        SERVER_HOST, auth_jwt_token);

    send_to_server(sockfd, request);
    char *response = receive_from_server(sockfd);
    close(sockfd);

    if (!response) {
        printf("ERROR: Nu s-a primit niciun răspuns din partea serverului.\n");
        return;
    }

    int code = status(response);
    if (code != 200) {
        fprintf(stderr, "FAIL: Cererea a eșuat cu codul %d.\n", code);
        free(response);
        return;
    }

    const char *body_start = strstr(response, "\r\n\r\n");
    if (body_start == NULL) {
        fprintf(stderr, "ERROR: Formatul răspunsului este invalid.\n");
        free(response);
        return;
    }
    body_start += strlen("\r\n\r\n");

    JSON_Value *json = json_parse_string(body_start);
    if (json == NULL) {
        fprintf(stderr, "ERROR: Eroare la parsarea răspunsului JSON.\n");
        free(response);
        return;
    }


    JSON_Object *root = json_value_get_object(json);
    JSON_Array *collections = json_object_get_array(root, "collections");

    if (!collections || json_array_get_count(collections) == 0) {
        printf("INFO: Nu există colecții disponibile.\n");
        json_value_free(json);
        free(response);
        return;
    }

    printf("SUCCESS: Lista colecțiilor disponibile:\n");
    for (size_t i = 0; i < json_array_get_count(collections); i++) {
        JSON_Object *collection = json_array_get_object(collections, i);
        int id = (int)json_object_get_number(collection, "id");
        const char *title = json_object_get_string(collection, "title");

        printf("#%d: %s\n", id, title ? title : "(fără titlu)");
    }

    json_value_free(json);
    free(response);
}

void add_movie_to_collection_prompt() {
    if (auth_jwt_token[0] == '\0') {
        puts("ERROR: Trebuie să ceri acces cu get_access înainte.");
        return;
    }

    int collectionId, movieId;

    printf("collection_id=");
    fflush(stdout);
    if (scanf("%d", &collectionId) != 1) {
        puts("ERROR: ID colecție invalid.");
        int c; while ((c = getchar()) != '\n' && c != EOF) {}
        return;
    }

    printf("movie_id=");
    fflush(stdout);
    if (scanf("%d", &movieId) != 1) {
        puts("ERROR: ID film invalid.");
        int c; while ((c = getchar()) != '\n' && c != EOF) {}
        return;
    }

    int c; while ((c = getchar()) != '\n' && c != EOF) {}

    add_movie_to_collection(collectionId, movieId);
}
void delete_movie_from_collection() {
    if (auth_jwt_token[0] == '\0') {
        puts("ERROR: Trebuie să ceri acces cu get_access înainte.");
        return;
    }

    int collection_id, movie_id;

    printf("collection_id=");
    fflush(stdout);
    if (scanf("%d", &collection_id) != 1) {
        puts("ERROR: ID colecție invalid.");
        int c; while ((c = getchar()) != '\n' && c != EOF) {}
        return;
    }

    printf("movie_id=");
    fflush(stdout);
    if (scanf("%d", &movie_id) != 1) {
        puts("ERROR: ID film invalid.");
        int c; while ((c = getchar()) != '\n' && c != EOF) {}
        return;
    }

    int c; while ((c = getchar()) != '\n' && c != EOF) {}

    char endpoint[150];
    sprintf(endpoint, "/api/v1/tema/library/collections/%d/movies/%d", collection_id, movie_id);

    int sockfd = open_connection(SERVER_HOST, SERVER_PORT);

    char *message = calloc(BUFFER, sizeof(char));
    sprintf(message,
        "DELETE %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Authorization: Bearer %s\r\n"
        "Connection: close\r\n\r\n",
        endpoint, SERVER_HOST, auth_jwt_token);

    send_to_server(sockfd, message);
    char *resp = receive_from_server(sockfd);

    if (!resp) {
        puts("FAIL: Nu s-a primit niciun răspuns.");
        free(message);
        close(sockfd);
        return;
    }

    int code = status(resp);
    switch (code) {
    case 200:
    case 204:
        puts("SUCCESS: Film șters din colecție");
        break;
    case 401:
        puts("ERROR: Acces neautorizat.");
        break;
    case 403:
        puts("ERROR: Nu sunteți owner al colecției.");
        break;
    case 404:
        puts("ERROR: Colecția sau filmul nu a fost găsit.");
        break;
    default:
        printf("FAIL: Ștergerea filmului a eșuat. Cod eroare: %d\n", code);
        break;
    }


    free(message);
    free(resp);
    close(sockfd);
}


int main() {
    char cmd[50];

    while (fgets(cmd, sizeof(cmd), stdin)) {

        cmd[strcspn(cmd, "\n")] = '\0';
        while (cmd[0] == ' ') memmove(cmd, cmd + 1, strlen(cmd));

        if (strcmp(cmd, "login_admin") == 0)
            login_admin();
        else if (strcmp(cmd, "login") == 0)
            login
        ();
        else if (strcmp(cmd, "add_user") == 0)
            add_user();
        else if (strcmp(cmd, "get_users") == 0)
            get_users();
        else if (strcmp(cmd, "delete_user") == 0)
            delete_user();
        else if (strcmp(cmd, "logout_admin") == 0)
            logout_admin();
        else if (strcmp(cmd, "logout") == 0)
            logout();
        else if (strcmp(cmd, "get_access") == 0)
            get_access();
        else if (strcmp(cmd, "add_movie") == 0)
            add_movie();
        else if (strcmp(cmd, "get_movies") == 0)
            get_movies();
        else if (strcmp(cmd, "get_movie") == 0)
            get_movie();
            else if (strcmp(cmd, "update_movie") == 0)
            update_movie();
            else if (strcmp(cmd, "delete_movie") == 0)
            delete_movie();
           else if (strcmp(cmd, "add_collection") == 0)
            add_collection();
            else if (strcmp(cmd, "add_movie_to_collection_p") == 0) {
                int collectionId, movieId;

                printf("collectionId=");
                fflush(stdout);
                if (scanf("%d", &collectionId) != 1) {
                    printf("ERROR: ID colecție invalid.\n");

                    int c; while ((c = getchar()) != '\n' && c != EOF) {}
                    continue;  
                }

                printf("movieId=");
                fflush(stdout);
                if (scanf("%d", &movieId) != 1) {
                    printf("ERROR: ID film invalid.\n");
                    int c; while ((c = getchar()) != '\n' && c != EOF) {}
                    continue;
                }

                int c; while ((c = getchar()) != '\n' && c != EOF) {}

                add_movie_to_collection(collectionId, movieId);
            }


        else if (strcmp(cmd, "get_collections") == 0)
        get_collections();
        else if (strcmp(cmd, "get_collection") == 0)
        get_collection();
        else if (strcmp(cmd, "delete_collection") == 0)
        delete_collection();
        else if (strcmp(cmd, "add_movie_to_collection") == 0)
        add_movie_to_collection_prompt();
        else if (strcmp(cmd, "delete_movie_from_collection") == 0)
        delete_movie_from_collection();

        else
            printf("Comandă necunoscută: %s\n", cmd);
        }

    return 0;
}
#include "os_mutex.h"
#include "cJSON_Process.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>

cJSON_Hooks g_cJson_hooks = {
#ifdef USE_FREERTOS
    .malloc_fn = __os_malloc,
    .free_fn   = __os_free,
#else
    .malloc_fn = malloc,
    .free_fn   = free,
#endif
};

cJSON *cJSON_Data_Init(void)
{
    cJSON *cJSON_Root = NULL;
    char *p = NULL;

    cJSON_InitHooks(&g_cJson_hooks);

    cJSON_Root = cJSON_CreateObject();
    if (NULL == cJSON_Root)
    {
        return NULL;
    }

    cJSON_AddStringToObject(cJSON_Root, NAME, DEFAULT_NAME);
    cJSON_AddNumberToObject(cJSON_Root, TEMP_NUM, DEFAULT_TEMP_NUM);
    cJSON_AddNumberToObject(cJSON_Root, HUM_NUM, DEFAULT_HUM_NUM);

    p = cJSON_Print(cJSON_Root);
    if (p != NULL)
    {
        g_cJson_hooks.free_fn(p);
    }

    return cJSON_Root;
}

uint8_t cJSON_Update(cJSON *object, const char *const string, void *d)
{
    cJSON *node = NULL;

    node = cJSON_GetObjectItem(object, string);
    if (node == NULL)
    {
        return UPDATE_FAIL;
    }

    if (node->type == cJSON_True || node->type == cJSON_False)
    {
        int *b = (int *)d;
        node->type = *b ? cJSON_True : cJSON_False;
        return UPDATE_SUCCESS;
    }
    else if (node->type == cJSON_String)
    {
        node->valuestring = (char *)d;
        return UPDATE_SUCCESS;
    }
    else if (node->type == cJSON_Number)
    {
        double *num = (double *)d;
        node->valuedouble = *num;
        node->valueint = (int)(*num);
        return UPDATE_SUCCESS;
    }

    return UPDATE_FAIL;
}

void Proscess(void *data)
{
    cJSON *root, *json_name, *json_temp_num, *json_hum_num;

    PRINT_DEBUG("开始解析JSON数据");
    root = cJSON_Parse((char *)data);
    if (root == NULL)
    {
        return;
    }

    json_name = cJSON_GetObjectItem(root, NAME);
    json_temp_num = cJSON_GetObjectItem(root, TEMP_NUM);
    json_hum_num = cJSON_GetObjectItem(root, HUM_NUM);

    if (json_name && json_temp_num && json_hum_num)
    {
        PRINT_DEBUG("name:%s\n temp_num:%f\n hum_num:%f\n",
                    json_name->valuestring,
                    json_temp_num->valuedouble,
                    json_hum_num->valuedouble);
    }

    cJSON_Delete(root);
}

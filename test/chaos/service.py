import json
import random
import time
import os
import requests
import sys


class Service:
    def __init__(self):
        self.namespace = 'cpp_sdk_test'
        self.name = 'polaris.cpp.sdk.chaos'
        self.token = ''


def CreateService(service, polaris_server):
    data = [{
        'namespace': service.namespace,
        'name': service.name,
        'owners': 'lambdaliu'
        }]
    response = requests.post('http://'+polaris_server+':8080/naming/v1/services', json=data)
    if response.status_code != 200:
        print 'create service ', service.name, 'failed'
        print(response.text)
        return False
    json_data = json.loads(response.text)
    service.token = json_data['responses'][0]['service']['token']
    print 'create service ', service.name, service.token
    return True


def DeleteService(service, polaris_server):
    data = [{
        'namespace': service.namespace,
        'name': service.name,
        'token': service.token
        }]
    response = requests.post('http://'+polaris_server+':8080/naming/v1/services/delete', json=data)
    if response.status_code != 200:
        print 'delete service ', service.name, 'failed'
        print(response.text)
        return False
    print 'delete service ', service.name
    return True


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print 'usage:', sys.argv[0], ' polaris_server'
        sys.exit(0)
    services = dict()
    with open('services.txt', 'r') as file:
        while True:
            line = file.readline()
            if not line:
                break
            print 'read line: ', line
            fields = line.rstrip().split(' ')
            if len(fields) == 4:
                key = int(fields[0])
                service = Service()
                service.namespace = fields[1]
                service.name = fields[2]
                service.token = fields[3]
                services[key] = service
                print 'init add service ', service.name

    while True:
        key = random.randint(0, 15)
        service_count = len(services)
        if key not in services:
            service = Service()
            service.name = service.name + str(key)
            if CreateService(service, sys.argv[1]):
                services[key] = service
        else:
            if DeleteService(services[key], sys.argv[1]):
                del services[key]
        if service_count != len(services):
            with open("new_services.txt", "w") as file:
                file.write("{0} {1}\n".format(time.time(), len(services)))
                for key, value in services.items():
                    file.write("{0} {1} {2} {3}\n".format(key, value.namespace, value.name, value.token))
            os.rename("new_services.txt", "services.txt")
        time.sleep(20)

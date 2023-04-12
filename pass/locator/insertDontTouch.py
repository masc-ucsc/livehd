import os

def readFile(fname):

    f = open( '../dinocpu/src/main/scala/' + fname,'r')
    readData = []
    count = 1
    for data in f.readlines():
        readData.append(data)
    
    return readData

def writeFile(fNewName, rData, indx, data):

    f = open( '../dinocpu/src/main/scala/' + fNewName, 'w+')

    count = 1
    for items in rData:
        if count == indx:
            f.write(data)
        f.write(items)
        count += 1

def processFile(fname, ind, nLine):
    print(fname)
    print(ind)
    print(nLine)

    # rData = readFile(fname)
    # writeFile(fname, rData, ind+1, nLine)

def main():
    pass
    # processFile(fname = 'pipelined/stage-register.scala', ind = 48, nLine = "gkjgk\n")

if __name__ == '__main__':
    main()
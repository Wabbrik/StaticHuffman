#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QFileDialog>
#include <QString>
#include <QMessageBox>
#include <QDir>
#include <QTextStream>
#include <iostream>
#include <vector>
#include <queue>
#include <unordered_map>
#include <stdio.h>
typedef unsigned char   U8;
typedef unsigned short  U16;
typedef unsigned int    U32;
typedef unsigned long long U64;

const int _histSize = 256;
FILE  *inFile, *outFile;
QString location;
U32 frequencies[256] = {0};

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
}

MainWindow::~MainWindow()
{
    delete ui;
}

struct Node
{
    char cha;
    int val;
    Node *left, *right;
};

struct CompGreater
{
    bool operator()(Node* left, Node* right) const { return left->val > right->val; }
};

Node* get_node(char ch, int val, Node* left, Node* right)
{
    Node* node = new Node();

    node->cha = ch;
    node->val = val;
    node->left = left;
    node->right = right;
    return node;
}

Node* build_tree(const U32(&frequencies)[_histSize]){
    std::priority_queue<Node*, std::vector<Node*>, CompGreater> HuffmanTree;

    for(int i = 0; i < _histSize; i++){
        if(frequencies[i] != 0){
            HuffmanTree.push(get_node(char(i), frequencies[i], nullptr, nullptr));
        }
    }

    while(HuffmanTree.size() != 1){
        Node *leftChild = HuffmanTree.top();
        HuffmanTree.pop();

        Node *rightChild = HuffmanTree.top();
        HuffmanTree.pop();

        int tempVal = leftChild->val + rightChild->val;
        HuffmanTree.push(get_node('\0', tempVal, leftChild, rightChild));
    }
    return HuffmanTree.top();
}

void get_charcodes(Node* root, std::vector<U32> const& bit, std::unordered_map<U8, std::vector<U32>> &outCodes){
    if(!root){
        return;
    }

    if(!root->left && !root->right){
        outCodes[root->cha] = bit;
    }

    std::vector<U32> leftBits = bit;
    leftBits.push_back(0);
    get_charcodes(root->left,leftBits, outCodes);

    std::vector<U32> rightBits = bit;
    rightBits.push_back(1);
    get_charcodes(root->right,rightBits, outCodes);
}

void decodePROTOTYPE(Node* root, int& bit, int limit){
    Node *node = root;
    for(int i = 0; i < limit; ){
        if(!node->left && !node->right){
            i++;
            node = root;
        }
        else{
            if(bit == 0) node = node->left;
            else node = node->right;
        }
    }
}

class Encoder{
public:
    Encoder(FILE* inFile, FILE* outFile);
    void encode(std::unordered_map<U8, std::vector<U32>> &codes);
    void flush();
    void get_frequencies(U32 (&frequencies)[_histSize]);
    void write_bit(U32 bit);
    void write_n_bits(U32 amount, U32 value);

private:
    FILE* inFile;
    FILE* outFile;

    U8 buffer;
    U32 nextBitPosition;
    void write_byte(U32 byte);
    void write_header(U32 (&frequencies)[_histSize]);
};

Encoder::Encoder(FILE* inFile, FILE* outFile) : inFile(inFile), outFile(outFile), buffer(1), nextBitPosition(0) {}

void Encoder::write_byte(U32 byte){
    putc(byte, outFile);
}

void Encoder::write_bit(U32 bit){
    buffer <<= 1;
    buffer |= (bit & 1);
    nextBitPosition += 1;

    if(nextBitPosition == 8){
        write_byte(buffer);
        buffer = 1;
        nextBitPosition = 0;
    }
}

void Encoder::write_n_bits(U32 amount, U32 value){
    value <<= 32 - amount;
    for(int i = 0; i < amount; i++){
        U32 bit = value >> 31;
        value <<= 1;
        write_bit(bit);
    }
}

void Encoder::write_header(U32 (&frequencies)[_histSize]){
    U32 fValues[_histSize] = {0};

    for(int i = 0; i < _histSize; i++){
        if(frequencies[i] == 0)
        {
            fValues[i] = 0;
            write_n_bits(2, fValues[i]);
        }
        else
        {
            if(frequencies[i] > 0 && frequencies[i] < UCHAR_MAX ){
                fValues[i] = 1;
                write_n_bits(2, fValues[i]);
            }
            else if(frequencies[i] >= UCHAR_MAX && frequencies[i] < USHRT_MAX){
                fValues[i] = 2;
                write_n_bits(2, fValues[i]);
            }
            else if(frequencies[i] >= USHRT_MAX && frequencies[i] < UINT_MAX){
                fValues[i] = 3;
                write_n_bits(2, fValues[i]);
            }
        }
    }

    for(int i = 0; i < _histSize; i++){
        if(fValues[i])
        {
            switch (fValues[i])
            {
            case 1:
                write_n_bits(8,frequencies[i]);
                break;
            case 2:
                write_n_bits(16,frequencies[i]);
                break;
            case 3:
                write_n_bits(32,frequencies[i]);
                break;
            }

        }
    }
}

void Encoder::get_frequencies(U32 (&frequencies)[_histSize]){
    int byte;
    while((byte = getc(inFile)) != EOF){
        ++frequencies[byte];
    }
    write_header(frequencies);
}

void Encoder::flush(){
    for(int i = 0; i < 7 ; i++) write_bit(0);
}

void Encoder::encode(std::unordered_map<U8, std::vector<U32>> &codes){
    int byte;
    fseek(inFile, 0, SEEK_SET);
    while((byte = getc(inFile)) != EOF){
        std::vector<U32> bitField = codes[byte];
        for(auto i = bitField.begin(); i != bitField.end(); i++){
            write_bit(*i);
        }
    }
}

class Decoder{
public:
    Decoder(FILE* inFile, FILE* outFile);
    U32 read_bit();
    U32 read_n_bits(U32 amount);
    void decode(U64 fileSize, Node *root);
    void parse_header(U32 (&frequencies)[_histSize]);
    void write_byte(U32 byte);
    U8 buffer;

private:
    FILE* inFile;
    FILE* outFile;
    U8 nextBitPosition;
    U8 read_byte();
};

Decoder::Decoder(FILE* inFile, FILE* outFile) : inFile(inFile), outFile(outFile), nextBitPosition(0) {buffer = read_byte();}

U8 Decoder::read_byte(){
    return getc(inFile) & 0xFF;
}

U32 Decoder::read_bit(){
    U32 val = buffer >> 7;
    buffer <<= 1;
    nextBitPosition += 1;

    if(nextBitPosition == 8){
        nextBitPosition = 0;
        buffer = read_byte();
    }

    return val;
}

U32 Decoder::read_n_bits(U32 amount){
    U32 retVal = 1;
    U32 retValMask = (1 << amount) - 1;

    for (int i = 0; i < amount; i++){
        retVal <<= 1;
        retVal |= read_bit();
    }
    if(amount < 32) return retVal & retValMask;
    return retVal;
}

void Decoder::write_byte(U32 byte){
    putc(byte, outFile);
}

void Decoder::parse_header(U32 (&frequencies)[_histSize]){
    for(int i = 0; i < 256; i++){
        frequencies[i] = read_n_bits(2);
    }

    for(int i = 0; i < 256; i++){
        if(frequencies[i]){
            switch (frequencies[i])
            {
            case 1:
                frequencies[i] = read_n_bits(8);
                break;
            case 2:
                frequencies[i] = read_n_bits(16);
                break;
            case 3:
                frequencies[i] = read_n_bits(32);
                break;
            }
        }
    }
}

void Decoder::decode(U64 fileSize, Node *root){
    Node *node = root;
    for(int i = 0; i < fileSize; ){
        U32 bit = read_bit();

        if(bit == 0) node = node->left;
        else
            if(bit == 1)node = node->right;

        if(!node->left && !node->right){
            write_byte(node->cha);
            i++;
            node = root;
        }
    }

}

void MainWindow::on_loadFileBtn_clicked()
{
    ui->decodeFileBtn->hide();
    ui->loadEncodedFileBtn->hide();
    location = QFileDialog::getOpenFileName(this, "Open a file", QDir::currentPath());
    inFile = fopen(location.toStdString().c_str(), "rb");
    if(!inFile){
        QMessageBox::critical(this, "Error while opening file at...", location);
        inFile = nullptr;
    }
}

std::unordered_map<U8, std::vector<U32>> codes;

void MainWindow::on_encodeFileBtn_clicked()
{
    location += ".gabi";
    outFile = fopen(location.toStdString().c_str(), "wb+");
    Encoder *en = new Encoder(inFile, outFile);
    en->get_frequencies(frequencies);
    Node* root = build_tree(frequencies);
    get_charcodes(root, std::vector<U32>(), codes);
    en->encode(codes);
    en->flush();

    fclose(inFile);
    fclose(outFile);
    delete root;
    ui->decodeFileBtn->show();
    ui->loadEncodedFileBtn->show();
}

void MainWindow::on_loadEncodedFileBtn_clicked()
{
    ui->loadFileBtn->hide();
    ui->encodeFileBtn->hide();
    location = QFileDialog::getOpenFileName(this, "Open a file", QDir::currentPath());
    inFile = fopen(location.toStdString().c_str(), "rb");
    if(!inFile){
        QMessageBox::critical(this, "Error while opening file at...", location);
        inFile = nullptr;
    }
}

void MainWindow::on_decodeFileBtn_clicked()
{
    location = location.left(location.lastIndexOf(QChar('.')));
    outFile = fopen(location.toStdString().c_str(), "wb+");
    Decoder *de = new Decoder(inFile, outFile);
    de->parse_header(frequencies);
    U64 fileSize = 0;
    for(U64 i = 0; i < 256; i++) {
        fileSize += frequencies[i];
    }

    Node *root = build_tree(frequencies);
    de->decode(fileSize, root);
    fclose(inFile);
    fclose(outFile);
    ui->loadFileBtn->show();
    ui->encodeFileBtn->show();
}

void MainWindow::on_encodeInputBtn_clicked()
{
    location = QDir::currentPath();
    location += "\\tempQtfile.txt";
    QByteArray bytes  = this->ui->plainTextEdit->toPlainText().toLatin1();
    char* characters = bytes.data();
    for(int i = 0; i < bytes.size(); i++){
        ++frequencies[characters[i]];
    }

    inFile = fopen(location.toStdString().c_str(), "wb+");

    Node* root = build_tree(frequencies);
    get_charcodes(root, std::vector<U32>(), codes);
    fclose(inFile);
    delete root;
}

void MainWindow::on_showCodesRadio_toggled(bool checked)
{
    QString c;
    if(!checked){
        ui->codeScreen->clear();
    }else{
        for(auto el : codes){
            c="";
            c += el.first;
            c += " : ";
            for(auto vel : el.second){
                c += QString::number(vel);
            }
            c += "\n";
            ui->codeScreen->appendPlainText(c);
        }
    }
}

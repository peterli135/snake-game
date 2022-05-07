// clang++ -std=c++20 -Wall -Werror -Wextra -Wpedantic -g3 -o team06-snake team06-snake.cpp

#include <iostream>
#include <vector>
#include <string>
#include <random>
#include <chrono>
#include <termios.h> // to control terminal modes
#include <unistd.h> // for read()
#include <cmath>
#include <fcntl.h> // to enable / disable non-blocking read()

using namespace std;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-const-variable"

// Constants: 
const char UP_CHAR       { 'w' };
const char DOWN_CHAR     { 's' };
const char LEFT_CHAR     { 'a' };
const char RIGHT_CHAR    { 'd' };
const char QUIT_CHAR     { 'q' };
const char NULL_CHAR     { 'z' };

const string ANSI_START { "\033[" };
const string START_COLOUR_PREFIX {"1;"};
const string START_COLOUR_SUFFIX {"m"};
const string STOP_COLOUR  {"\033[0m"};

const int widthOfMap = 50;
const int heightOfMap = 20;

const unsigned int COLOUR_IGNORE  { 0 };
const unsigned int COLOUR_RED     { 31 };
const unsigned int COLOUR_GREEN   { 32 };
const unsigned int COLOUR_CYAN    { 36 };
const unsigned int COLOUR_WHITE   { 37 };

// Types: 
struct position { int row; int col; };

typedef vector< string > stringvector;

stringvector snakeSprite
{
{"O"}
};

// Globals: 

struct termios initialTerm;
default_random_engine generator;
uniform_int_distribution<int> foodWidthPositionNumber(3, 20);
uniform_int_distribution<int> foodHeightPositionNumber(3, 49);

bool gameOver = false;
int tailPositionX [100];
int tailPositionY [100];
int lengthOfTail {0};

#pragma clang diagnostic pop

auto SetupScreenAndInput() -> void
{
    struct termios newTerm;
    // Load the current terminal attributes for STDIN and store them in a global
    tcgetattr(fileno(stdin), &initialTerm);
    newTerm = initialTerm;
    // Mask out terminal echo and enable "noncanonical mode"
    // " ... input is available immediately (without the user having to type 
    // a line-delimiter character), no input processing is performed ..."
    newTerm.c_lflag &= ~ICANON;
    newTerm.c_lflag &= ~ECHO;
    // Set the terminal attributes for STDIN immediately
    tcsetattr(fileno(stdin), TCSANOW, &newTerm);
}
auto TeardownScreenAndInput() -> void
{
    // Reset STDIO to its original settings
    tcsetattr(fileno(stdin), TCSANOW, &initialTerm);
}
auto SetNonblockingReadState( bool desiredState = true ) -> void
{
    auto currentFlags { fcntl( 0, F_GETFL ) };
    if ( desiredState ) { fcntl( 0, F_SETFL, ( currentFlags | O_NONBLOCK ) ); }
    else { fcntl( 0, F_SETFL, ( currentFlags & ( ~O_NONBLOCK ) ) ); }
    cerr << "SetNonblockingReadState [" << desiredState << "]" << endl;
}
// Everything from here on is based on ANSI codes
auto ClearScreen() -> void { cout << ANSI_START << "2J" ; }
auto MoveTo( unsigned int x, unsigned int y ) -> void { cout << ANSI_START << x << ";" << y << "H" ; }
auto HideCursor() -> void { cout << ANSI_START << "?25l" ; }
auto ShowCursor() -> void { cout << ANSI_START << "?25h" ; }
auto GetTerminalSize() -> position
{
    // This feels sketchy but is actually about the only way to make this work
    MoveTo(999,999);
    cout << ANSI_START << "6n" ; // ask for Device Status Report 
    string responseString;
    char currentChar { static_cast<char>( getchar() ) };
    while ( currentChar != 'R')
    {
        responseString += currentChar;
        currentChar = getchar();
    }
    // format is ESC[nnn;mmm ... so remove the first 2 characters + split on ; + convert to unsigned int
    // cerr << responseString << endl;
    responseString.erase(0,2);
    // cerr << responseString << endl;
    auto semicolonLocation = responseString.find(";");
    // cerr << "[" << semicolonLocation << "]" << endl;
    auto rowsString { responseString.substr( 0, semicolonLocation ) };
    auto colsString { responseString.substr( ( semicolonLocation + 1 ), responseString.size() ) };
    // cerr << "[" << rowsString << "][" << colsString << "]" << endl;
    auto rows = stoul( rowsString );
    auto cols = stoul( colsString );
    position returnSize { static_cast<int>(rows), static_cast<int>(cols) };
    // cerr << "[" << returnSize.row << "," << returnSize.col << "]" << endl;
    return returnSize;
}
auto MakeColour( string inputString, 
                 const unsigned int foregroundColour = COLOUR_WHITE,
                 const unsigned int backgroundColour = COLOUR_IGNORE ) -> string
{
    string outputString;
    outputString += ANSI_START;
    outputString += START_COLOUR_PREFIX;
    outputString += to_string( foregroundColour );
    if ( backgroundColour ) 
    { 
        outputString += ";";
        outputString += to_string( ( backgroundColour + 10 ) ); // Tacky but works
    }
    outputString += START_COLOUR_SUFFIX;
    outputString += inputString;
    outputString += STOP_COLOUR;
    return outputString;
}


auto CreateBoundariesMap() -> void 
{
    // create the top boundary of the map
    for (int topBoundary = 0; topBoundary < (widthOfMap + 2); topBoundary++) {
        cout << "#";
    }
    cout << endl;

    // create left and right boundary of the map
    for (int leftBoundary = 0; leftBoundary < heightOfMap; leftBoundary++) {
        for (int rightBoundary = 0; rightBoundary <= widthOfMap; rightBoundary++) {
            if (rightBoundary == 0) {
                cout << "#"; // creates the left boundary of the map
            } 
            else {
                cout << " "; // creates the open spaces on the map
            }
            if (rightBoundary == widthOfMap) {
                cout << "#"; // creates the right boundary of the map
            }
        }
        cout << endl;
    }

    // create bottom boundary of the map
    for (int bottomBoundary = 0; bottomBoundary < (widthOfMap + 2); bottomBoundary++) {
        cout << "#";
    }
    cout << endl;
}

// Draws the snake figure and logic for the tail to follow position of head
auto DrawSprite( position targetPosition,
                 stringvector sprite,
                 const unsigned int foregroundColour = COLOUR_GREEN,
                 const unsigned int backgroundColour = COLOUR_IGNORE)
{
    // Creates the snake head figure
    MoveTo( targetPosition.row, targetPosition.col );
    cout << MakeColour( sprite[0], foregroundColour, backgroundColour );
    MoveTo( ( targetPosition.row + 1 ) , targetPosition.col );
    
    // Creates the tail figure (logic is based off of how the tail must follow the coordinates of where the previous position of tail was at)
    int previousCoordinateX = tailPositionX[0];
    int previousCoordinateY = tailPositionY[0];
    int previousCoordinateTwoX, previousCoordinateTwoY;
    tailPositionX[0] = targetPosition.row;
    tailPositionY[0] = targetPosition.col;
    for ( auto currentSpriteRow = 0 ; currentSpriteRow < static_cast<int>(sprite.size()) ; currentSpriteRow++ ) 
    {
        for (int currentSnakePosition = 1; currentSnakePosition < lengthOfTail; currentSnakePosition++) 
        {
            previousCoordinateTwoX = tailPositionX[currentSnakePosition];
            previousCoordinateTwoY = tailPositionY[currentSnakePosition];
            tailPositionX[currentSnakePosition] = previousCoordinateX;
            tailPositionY[currentSnakePosition] = previousCoordinateY;
            previousCoordinateX = previousCoordinateTwoX;
            previousCoordinateY = previousCoordinateTwoY;
            MoveTo( previousCoordinateX, previousCoordinateY );
            cout << MakeColour( sprite[currentSnakePosition], foregroundColour, backgroundColour );
            MoveTo( ( targetPosition.row + ( currentSnakePosition ) ) , targetPosition.col );
        }
        // need to account for if snake eats 1 food, then tail length is 1
        if (lengthOfTail == 1)
        {
            int currentSnakePosition = 1;
            previousCoordinateTwoX = tailPositionX[currentSnakePosition];
            previousCoordinateTwoY = tailPositionY[currentSnakePosition];
            tailPositionX[currentSnakePosition] = previousCoordinateX;
            tailPositionY[currentSnakePosition] = previousCoordinateY;
            previousCoordinateX = previousCoordinateTwoX;
            previousCoordinateY = previousCoordinateTwoY;
            MoveTo( previousCoordinateX, previousCoordinateY );
            cout << MakeColour( sprite[currentSnakePosition], foregroundColour, backgroundColour );
            MoveTo( ( targetPosition.row + ( currentSnakePosition ) ) , targetPosition.col );
        }
    }
    for (int snakeHead = 1; snakeHead < lengthOfTail; snakeHead++)
    {
        if (tailPositionX[snakeHead] == targetPosition.row && tailPositionY[snakeHead] == targetPosition.col) {gameOver = true;};
    }
}
auto DrawFood( position foodPosition,
               const unsigned int foregroundColour = COLOUR_RED,
               const unsigned int backgroundColour = COLOUR_IGNORE)
{
    cout << MakeColour("X", foregroundColour, backgroundColour);
    MoveTo( foodPosition.row, foodPosition.col );
}
auto DrawScore ( position scorePosition,
               const unsigned int foregroundColour = COLOUR_CYAN,
               const unsigned int backgroundColour = COLOUR_IGNORE)
{
    cout << MakeColour("Score: ", foregroundColour, backgroundColour);
    cout << MakeColour(to_string(lengthOfTail), foregroundColour, backgroundColour);
    MoveTo( scorePosition.row, scorePosition.col );
}

auto main() -> int
{
    SetupScreenAndInput();
    // Check that the terminal size is large enough for our fishies
    const position TERMINAL_SIZE { GetTerminalSize() };
    if ( ( TERMINAL_SIZE.row < 25 ) or ( TERMINAL_SIZE.col < 50 ) )
    {
        ShowCursor();
        TeardownScreenAndInput();
        cout << endl <<  "Terminal window must be at least 25 by 50 to run this game" << endl;
        return EXIT_FAILURE;
    }

    // 1. Initialize State
    int score = 0;

    position currentPosition {2,2}; // snake position starting point

    int foodWidthPosition = foodWidthPositionNumber(generator);
    int foodHeightPosition = foodHeightPositionNumber(generator);
    position foodPosition = {foodWidthPosition, foodHeightPosition}; // food position starting point

    position scorePosition = {23, 1}; // scoreboard

    bool allowBackgroundProcessing { true };
    char currentChar = 'z';

    auto startTimestamp { chrono::steady_clock::now() };
    auto endTimestamp { startTimestamp };
    int elapsedTimePerTick { 100 }; // tick is set at 100ms

    auto startTimestampKey { chrono::steady_clock::now() };
    auto endTimestampKey { startTimestampKey };
    int elapsedTimePerKey { 500 }; // key is set at 500ms

    SetNonblockingReadState( allowBackgroundProcessing );
    ClearScreen();
    HideCursor();

    while (!gameOver)
    {
        endTimestamp = chrono::steady_clock::now();
        auto elapsed { chrono::duration_cast<chrono::milliseconds>( endTimestamp - startTimestamp ).count() };

        if (elapsed >= elapsedTimePerTick)
        {
            endTimestampKey = chrono::steady_clock::now();
            auto elapsedKeyPress { chrono::duration_cast<chrono::milliseconds>( endTimestampKey - startTimestampKey ).count() };

            if (elapsedKeyPress <= elapsedTimePerKey)
            {
                read(0, &currentChar, 1);
                // Clear inputs in preparation for the next input
                startTimestampKey = endTimestampKey;
            }

            // 2. Update State
            if ( currentChar == UP_CHAR )    { currentPosition.row = max(  0, (currentPosition.row - 1) ); }
            if ( currentChar == DOWN_CHAR )  { currentPosition.row = min( 22, (currentPosition.row + 1) ); }
            if ( currentChar == LEFT_CHAR )  { currentPosition.col = max(  1, (currentPosition.col - 1) ); }
            if ( currentChar == RIGHT_CHAR ) { currentPosition.col = min( 52, (currentPosition.col + 1) ); }

            // Checks if the snake head touches the food (if it is in the exact position coordinates of food)
            if (currentPosition.row == foodPosition.row && currentPosition.col == foodPosition.col) {
                foodWidthPosition = foodWidthPositionNumber(generator);
                foodHeightPosition = foodHeightPositionNumber(generator);
                foodPosition.row = foodWidthPosition;
                foodPosition.col = foodHeightPosition;
                snakeSprite.push_back("o");
                score += 1;
                lengthOfTail += 1;
            }

            // Checks if snake head touches the boundary
            if (currentPosition.row == 1 || currentPosition.row == 22 || currentPosition.col == 1 || currentPosition.col == 52) {
                gameOver = true;
            }

            ClearScreen();
            HideCursor(); // sometimes the Visual Studio Code terminal seems to forget
            CreateBoundariesMap();

            // 3.B Draw snake based on state
            MoveTo( currentPosition.row, currentPosition.col );
            DrawSprite( {currentPosition.row, currentPosition.col}, snakeSprite );
            MoveTo( 1, 1 );
            cout << "[" << currentPosition.row << "," << currentPosition.col << "]" << endl;

            // 3.B Draw food based on state
            MoveTo( foodPosition.row, foodPosition.col );
            DrawFood( {foodPosition.row, foodPosition.col} );
            cout << "" << endl;
            MoveTo( 1, 1 );

            // 4. Draw the scoreboard on bottom
            MoveTo( scorePosition.row, scorePosition.col );
            DrawScore( {scorePosition.row, scorePosition.col} );
            cout << "" << endl;
            MoveTo( 1, 1 );

            // makes the game more playable or snake sprite would be moving too fast
            usleep(150000); // set at 150ms

            // Clear inputs in preparation for the next input
            startTimestampKey = endTimestampKey;

        // Clear inputs in preparation for the next input
        startTimestamp = endTimestamp;
        }
    }
    currentChar = NULL_CHAR;
    ClearScreen();
    MoveTo(1, 1);
    auto line1 = "+ * * * * * * * * * * * * * +";
    auto line2 = "*                           *"; 
    cerr << line1 << '\n' << line2 << '\n' << "*  Your total score was: " << score << "  *" << '\n' << "*  Better luck next time :) *" << '\n' << line2 << '\n' << line1 << endl;
    // N. Tidy Up and Close Down
    ShowCursor();
    SetNonblockingReadState( false );
    TeardownScreenAndInput();
    cout << endl; // be nice to the next command
    return EXIT_SUCCESS;
}

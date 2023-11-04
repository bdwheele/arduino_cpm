/* FDC for Arduino CP/M 
   Use the library from https://github.com/dhansel/ArduinoFDC
   to control a floppy drive (3.5" 1.44M disk).

   Eventually use an arduino connected via I2C upstream to control
   what is read and written.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
  
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
 */
 
/* Wiring....
--- Floppy Cable ---
Name       Leon     Floppy 
VCC        VCC      Power Pin 1
GND        GND      Power Pin 2
GND        GND      Any odd-numbered pin
STEP       A2       20
STEPDIR    A3       18
READDATA   4        30   Per the docs, a 1K pullup to 5v may be required depending on setup.
MOTORA     5        10
SELECTA    6        14
SIDE       7        32
INDEX      8        8
WRITEDATA  9        22
WRITEGATE  10       24
TRACK0     14       26
WRITEPROT  15       28
DENSITY    16       2
MOTORB     A0       16
SELECTB    A1       12

*/






#include "ArduinoFDC.h"


// -------------------------------------------------------------------------------------------------
// Basic helper functions
// -------------------------------------------------------------------------------------------------

#define TEMPBUFFER_SIZE 80
byte tempbuffer[TEMPBUFFER_SIZE];

unsigned long motor_timeout = 0;


void print_hex(byte b)
{
  if( b<16 ) Serial.write('0');
  Serial.print(b, HEX);
}


void dump_buffer(int offset, byte *buf, int n)
{
  int i = 0;
  while( i<n )
    {
      print_hex((offset+i)/256); 
      print_hex((offset+i)&255); 
      Serial.write(':');

      for(int j=0; j<16; j++)
        {
          if( (j&7)==0  ) Serial.write(' ');
          if( i+j<n ) print_hex(buf[i+j]); else Serial.print(F("  "));
          Serial.write(' ');
        }

      Serial.write(' ');
      for(int j=0; j<16; j++)
        {
          if( (j&7)==0  ) Serial.write(' ');
          if( i+j<n ) Serial.write(isprint(buf[i+j]) ? buf[i+j] : '.'); else Serial.write(' ');
        }

      Serial.println();
      i += 16;
    }
}


char *read_user_cmd(void *buffer, int buflen)
{
  char *buf = (char *) buffer;
  byte l = 0;
  do
    {
      int i = Serial.read();

      if( (i==13 || i==10) )
        { Serial.println(); break; }
      else if( i==27 )
        { l=0; Serial.println(); break; }
      else if( i==8 )
        { 
          if( l>0 )
            { Serial.write(8); Serial.write(' '); Serial.write(8); l--; }
        }
      else if( isprint(i) && l<buflen-1 )
        { buf[l++] = i; Serial.write(i); }
      
      if( motor_timeout>0 && millis() > motor_timeout ) { ArduinoFDC.motorOff(); motor_timeout = 0; }
    }
  while(true);

  while( l>0 && isspace(buf[l-1]) ) l--;
  buf[l] = 0;
  return buf;
}


bool confirm_formatting()
{
  int c;
  Serial.print(F("Formatting will erase all data on the disk in drive "));
  Serial.write('A' + ArduinoFDC.selectedDrive());
  Serial.print(F(". Continue (y/n)?"));
  while( (c=Serial.read())<0 );
  do { delay(1); } while( Serial.read()>=0 );
  Serial.println();
  return c=='y';
}


void print_drive_type(byte n)
{
  switch( n )
    {
    case ArduinoFDCClass::DT_5_DD: Serial.print(F("5.25\" DD")); break;
    case ArduinoFDCClass::DT_5_DDonHD: Serial.print(F("5.25\" DD disk in HD drive")); break;
    case ArduinoFDCClass::DT_5_HD: Serial.print(F("5.25\" HD")); break;
    case ArduinoFDCClass::DT_3_DD: Serial.print(F("3.5\" DD")); break;
    case ArduinoFDCClass::DT_3_HD: Serial.print(F("3.5\" HD")); break;
    default: Serial.print(F("Unknown"));
    }
}


void print_error(byte n)
{
  Serial.print(F("Error: "));
  switch( n )
    {
    case S_OK        : Serial.print(F("No error")); break;
    case S_NOTINIT   : Serial.print(F("ArduinoFDC.begin() was not called")); break;
    case S_NOTREADY  : Serial.print(F("Drive not ready")); break;
    case S_NOSYNC    : Serial.print(F("No sync marks found")); break;
    case S_NOHEADER  : Serial.print(F("Sector header not found")); break;
    case S_INVALIDID : Serial.print(F("Data record has unexpected id")); break;
    case S_CRC       : Serial.print(F("Data checksum error")); break;
    case S_NOTRACK0  : Serial.print(F("No track 0 signal detected")); break;
    case S_VERIFY    : Serial.print(F("Verify after write failed")); break;
    case S_READONLY  : Serial.print(F("Disk is write protected")); break;
    default          : Serial.print(F("Unknonwn error")); break;
    }
  Serial.println('!');
}


void set_drive_type(int n)
{
  ArduinoFDC.setDriveType((ArduinoFDCClass::DriveType) n);
  Serial.print(F("Setting disk type for drive ")); Serial.write('A'+ArduinoFDC.selectedDrive());
  Serial.print(F(" to ")); print_drive_type(ArduinoFDC.getDriveType());
  Serial.println();
}

static byte databuffer[516];


void monitor() 
{
  char cmd;
  int a1, a2, a3, head, track, sector, n;
  
  ArduinoFDC.motorOn();
  while( true )
    {
      Serial.print(F("\r\n\r\nCommand: "));
      n = sscanf(read_user_cmd(tempbuffer, 512), "%c%i,%i,%i", &cmd, &a1, &a2, &a3);
      if( n<=0 || isspace(cmd) ) continue;

      if( cmd=='r' && n>=3 )
        {
          track=a1; sector=a2; head= (n==3) ? 0 : a3;
          if( head>=0 && head<2 && track>=0 && track<ArduinoFDC.numTracks() && sector>=1 && sector<=ArduinoFDC.numSectors() )
            {
              Serial.print(F("Reading track ")); Serial.print(track); 
              Serial.print(F(" sector ")); Serial.print(sector);
              Serial.print(F(" side ")); Serial.println(head);
              Serial.flush();

              byte status = ArduinoFDC.readSector(track, head, sector, databuffer);
              if( status==S_OK )
                {
                  dump_buffer(0, databuffer+1, 512);
                  Serial.println();
                }
              else
                print_error(status);
            }
          else
            Serial.println(F("Invalid sector specification"));
        }
      else if( cmd=='r' && n==1 )
        {
          ArduinoFDC.motorOn();
          for(track=0; track<ArduinoFDC.numTracks(); track++)
            for(head=0; head<2; head++)
              {
                sector = 1;
                for(byte i=0; i<ArduinoFDC.numSectors(); i++)
                  {
                    byte attempts = 0;
                    while( true )
                      {
                        Serial.print(F("Reading track ")); Serial.print(track); 
                        Serial.print(F(" sector ")); Serial.print(sector);
                        Serial.print(F(" side ")); Serial.print(head);
                        Serial.flush();
                        byte status = ArduinoFDC.readSector(track, head, sector, databuffer);
                        if( status==S_OK )
                          {
                            Serial.println(F(" => ok"));
                            break;
                          }
                        else if( (status==S_INVALIDID || status==S_CRC) && (attempts++ < 10) )
                          Serial.println(F(" => CRC error, trying again"));
                        else
                          {
                            Serial.print(F(" => "));
                            print_error(status);
                            break;
                          }
                      }

                    sector+=2;
                    if( sector>ArduinoFDC.numSectors() ) sector = 2;
                  }
              }
        }
      else if( cmd=='w' && n>=3 )
        {
          track=a1; sector=a2; head= (n==3) ? 0 : a3;
          if( head>=0 && head<2 && track>=0 && track<ArduinoFDC.numTracks() && sector>=1 && sector<=ArduinoFDC.numSectors() )
            {
              Serial.print(F("Writing and verifying track ")); Serial.print(track); 
              Serial.print(F(" sector ")); Serial.print(sector);
              Serial.print(F(" side ")); Serial.println(head);
              Serial.flush();
          
              byte status = ArduinoFDC.writeSector(track, head, sector, databuffer, true);
              if( status==S_OK )
                Serial.println(F("Ok."));
              else
                print_error(status);
            }
          else
            Serial.println(F("Invalid sector specification"));
        }
      else if( cmd=='w' && n>=1 )
        {
          bool verify = n>1 && a2>0;
          char c;
          Serial.print(F("Write current buffer to all sectors in drive "));
          Serial.write('A' + ArduinoFDC.selectedDrive());
          Serial.println(F(". Continue (y/n)?"));
          while( (c=Serial.read())<0 );
          if( c=='y' )
            {
              ArduinoFDC.motorOn();
              for(track=0; track<ArduinoFDC.numTracks(); track++)
                for(head=0; head<2; head++)
                  {
                    sector = 1;
                    for(byte i=0; i<ArduinoFDC.numSectors(); i++)
                      {
                        Serial.print(verify ? F("Writing and verifying track ") : F("Writing track ")); Serial.print(track);
                        Serial.print(F(" sector ")); Serial.print(sector);
                        Serial.print(F(" side ")); Serial.print(head);
                        Serial.flush();
                        byte status = ArduinoFDC.writeSector(track, head, sector, databuffer, verify);
                        if( status==S_OK )
                          Serial.println(F(" => ok"));
                        else
                          {
                            Serial.print(F(" => "));
                            print_error(status);
                          }

                        sector+=2;
                        if( sector>ArduinoFDC.numSectors() ) sector = 2;
                      }
                  }
            }
        }
      else if( cmd=='b' )
        {
          Serial.println(F("Buffer contents:"));
          dump_buffer(0, databuffer+1, 512);
        }
      else if( cmd=='B' )
        {
          Serial.print(F("Filling buffer"));
          if( n==1 )
            {
              for(int i=0; i<512; i++) databuffer[i+1] = i;
            }
          else
            {
              Serial.print(F(" with 0x"));
              Serial.print(a1, HEX);
              for(int i=0; i<512; i++) databuffer[i+1] = a1;
            }
          Serial.println();
        }
      else if( cmd=='m' )
        {
          if( n==1 )
            {
              Serial.print(F("Drive "));
              Serial.write('A' + ArduinoFDC.selectedDrive());
              Serial.print(F(" motor is "));
              Serial.println(ArduinoFDC.motorRunning() ? F("on") : F("off"));
            }
          else
            {
              Serial.print(F("Turning drive "));
              Serial.write('A' + ArduinoFDC.selectedDrive());
              Serial.print(F(" motor "));
              if( n==1 || a1==0 )
                { 
                  Serial.println(F("off")); 
                  ArduinoFDC.motorOff();
                }
              else
                { 
                  Serial.println(F("on")); 
                  ArduinoFDC.motorOn();
                }
            }
        }
      else if( cmd=='s' )
        {
          if( n==1 )
            {
              Serial.print(F("Current drive is "));
              Serial.write('A' + ArduinoFDC.selectedDrive());
            }
          else
            {
              Serial.print(F("Selecting drive "));
              Serial.write(a1>0 ? 'B' : 'A');
              Serial.println();
              ArduinoFDC.selectDrive(n>1 && a1>0);
              ArduinoFDC.motorOn();
            }
        }
      else if( cmd=='t' && n>1 )
        {
          set_drive_type(a1);
        }
      else if( cmd=='f' )
        {
          if( confirm_formatting() )
            {
              Serial.println(F("Formatting disk..."));
              byte status = ArduinoFDC.formatDisk(databuffer, n>1 ? a1 : 0, n>2 ? a2 : 255);
              if( status!=S_OK ) print_error(status);
              memset(databuffer, 0, 513);
            }
        }

      // must save flash space if all three of ARDUDOS/MONITR/XMODEM are enabled on UNO
      else if( cmd=='h' || cmd=='?' )
        {
          Serial.println(F("Commands (t=track (0-based), s=sector (1-based), h=head (0/1)):"));
          Serial.println(F("r t,s,h  Read sector to buffer and print buffer"));
          Serial.println(F("r        Read ALL sectors and print pass/fail"));
          Serial.println(F("w t,s,h  Write buffer to sector"));
          Serial.println(F("w [0/1]  Write buffer to ALL sectors (without/with verify)"));
          Serial.println(F("b        Print buffer"));
          Serial.println(F("B [n]    Fill buffer with 'n' or 00..FF if n not given"));
          Serial.println(F("m 0/1    Turn drive motor off/on"));
          Serial.println(F("s 0/1    Select drive A/B"));
          Serial.println(F("t 0-4    Set type of current drive (5.25DD/5.25DDinHD/5.25HD/3.5DD/3.5HD)"));
          Serial.println(F("f        Low-level format disk (tf)"));
        }
      else
        Serial.println(F("Invalid command"));
    }
}



// -------------------------------------------------------------------------------------------------
// Main functions
// -------------------------------------------------------------------------------------------------


void setup() 
{
  Serial.begin(115200);
  ArduinoFDC.begin(ArduinoFDCClass::DT_3_HD, ArduinoFDCClass::DT_3_HD);

  Serial.print(F("Drive A: ")); print_drive_type(ArduinoFDC.getDriveType()); Serial.println();
  if( ArduinoFDC.selectDrive(1) )
    {
      Serial.print(F("Drive B: ")); print_drive_type(ArduinoFDC.getDriveType()); Serial.println();
      ArduinoFDC.selectDrive(0);
    }

}


void loop() 
{
  monitor();
}
